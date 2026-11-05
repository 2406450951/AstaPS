package emu.grasscutter.tools.protocol;

import com.google.protobuf.DescriptorProtos.FileDescriptorProto;
import com.google.protobuf.DescriptorProtos.FileDescriptorSet;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HexFormat;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.TreeSet;
import java.util.stream.Stream;

/** Regenerates Java protocol sources from the canonical 7.1 descriptor set and recovered additions. */
public final class ProtocolJavaGenerator {
    private static final int FILES_PER_PROTOC_INVOCATION = 128;

    private ProtocolJavaGenerator() {}

    public static void main(String[] args) throws Exception {
        if (args.length != 6 && args.length != 8) {
            throw new IllegalArgumentException(
                    "usage: ProtocolJavaGenerator <protoc> <descriptor-set> <file-list>"
                        + " <supplemental-proto-dir> <supplemental-import-dir> <java-out>"
                        + " [audit-descriptor-out generator-source-dir]");
        }

        var protoc = Path.of(args[0]).toAbsolutePath().normalize();
        var descriptorSet = Path.of(args[1]).toAbsolutePath().normalize();
        var fileList = Path.of(args[2]).toAbsolutePath().normalize();
        var supplementalProtoDir = Path.of(args[3]).toAbsolutePath().normalize();
        var supplementalImportDir = Path.of(args[4]).toAbsolutePath().normalize();
        var javaOut = Path.of(args[5]).toAbsolutePath().normalize();
        var auditOut = args.length == 8 ? Path.of(args[6]).toAbsolutePath().normalize() : null;
        var generatorSourceDir =
                args.length == 8 ? Path.of(args[7]).toAbsolutePath().normalize() : null;
        if (auditOut != null) {
            if (javaOut.startsWith(generatorSourceDir) || generatorSourceDir.startsWith(javaOut)) {
                throw new IllegalArgumentException("Generator sources and Java output must not overlap");
            }
            if (auditOut.equals(protoc)
                    || auditOut.startsWith(generatorSourceDir)
                    || auditOut.equals(descriptorSet)
                    || auditOut.equals(fileList)
                    || auditOut.startsWith(javaOut)
                    || auditOut.startsWith(supplementalProtoDir)
                    || auditOut.startsWith(supplementalImportDir)) {
                throw new IllegalArgumentException(
                        "Audit output must not overwrite protocol inputs or Java output");
            }
            Files.deleteIfExists(auditOut);
            Files.deleteIfExists(Path.of(auditOut + ".json"));
        }

        requireRegularFile(protoc, "protoc executable");
        requireRegularFile(descriptorSet, "descriptor set");
        requireRegularFile(fileList, "protocol file list");
        requireDirectory(supplementalProtoDir, "supplemental proto directory");
        requireDirectory(supplementalImportDir, "supplemental import stub directory");
        var generatorSourcesHash =
                generatorSourceDir == null ? null : generatorFingerprint(generatorSourceDir);

        var protocolFiles = Files.readAllLines(fileList, StandardCharsets.UTF_8).stream()
                .map(String::trim)
                .filter(line -> !line.isEmpty())
                .distinct()
                .sorted()
                .toList();
        if (protocolFiles.isEmpty()) {
            throw new IllegalStateException("protocol file list is empty: " + fileList);
        }

        var supplementalProtoFiles = listProtoFiles(supplementalProtoDir);
        var allProtocolFiles = new TreeSet<String>();
        allProtocolFiles.addAll(protocolFiles);
        allProtocolFiles.addAll(supplementalProtoFiles);
        var overlappingFiles = protocolFiles.stream().filter(supplementalProtoFiles::contains).toList();
        var expectedJavaFileCount = allProtocolFiles.size();

        recreateDirectory(javaOut);

        Path temporaryDirectory = null;
        byte[] auditDescriptor = null;
        try {
            temporaryDirectory = Files.createTempDirectory("astaps-protocol-descriptors-");
            var supplementalDescriptorSet = temporaryDirectory.resolve("supplemental.desc");
            var combinedDescriptorSet = temporaryDirectory.resolve("combined.desc");

            FileDescriptorSet supplementalDescriptors = FileDescriptorSet.getDefaultInstance();
            if (!supplementalProtoFiles.isEmpty()) {
                runSupplementalDescriptorProtoc(
                        protoc,
                        supplementalProtoDir,
                        supplementalImportDir,
                        supplementalDescriptorSet,
                        supplementalProtoFiles);
                supplementalDescriptors =
                        FileDescriptorSet.parseFrom(Files.readAllBytes(supplementalDescriptorSet));
            }

            var baseDescriptors = FileDescriptorSet.parseFrom(Files.readAllBytes(descriptorSet));
            var combinedDescriptors = mergeDescriptors(baseDescriptors, supplementalDescriptors);
            auditDescriptor = combinedDescriptors.toByteArray();
            Files.write(combinedDescriptorSet, auditDescriptor);

            var filesToGenerate = List.copyOf(allProtocolFiles);
            for (int offset = 0; offset < filesToGenerate.size(); offset += FILES_PER_PROTOC_INVOCATION) {
                var end = Math.min(offset + FILES_PER_PROTOC_INVOCATION, filesToGenerate.size());
                runDescriptorProtoc(
                        protoc, combinedDescriptorSet, javaOut, filesToGenerate.subList(offset, end));
            }
        } finally {
            if (temporaryDirectory != null) {
                deleteDirectory(temporaryDirectory);
            }
        }

        long javaFileCount;
        try (Stream<Path> generatedFiles = Files.walk(javaOut)) {
            javaFileCount = generatedFiles
                    .filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().endsWith(".java"))
                    .count();
        }
        if (javaFileCount != expectedJavaFileCount) {
            throw new IllegalStateException(
                    "protoc generated "
                            + javaFileCount
                            + " Java files; expected "
                            + expectedJavaFileCount
                            + " from "
                            + protocolFiles.size()
                            + " base descriptor files and "
                            + supplementalProtoFiles.size()
                            + " supplemental proto files ("
                            + overlappingFiles.size()
                            + " overrides)");
        }

        if (auditOut != null) {
            Files.createDirectories(auditOut.getParent());
            Files.write(auditOut, auditDescriptor);
            String metadata =
                    "{\"format\":\"astaps.server-protocol-descriptor.v1\","
                            + "\"descriptor_sha256\":\""
                            + sha256(auditDescriptor)
                            + "\","
                            + "\"base_sha256\":\""
                            + sha256(Files.readAllBytes(descriptorSet))
                            + "\","
                            + "\"proto_sources_sha256\":\""
                            + sourceFingerprint(supplementalProtoDir, supplementalImportDir)
                            + "\","
                            + "\"generator_sources_sha256\":\""
                            + generatorSourcesHash
                            + "\"}\n";
            Files.writeString(Path.of(auditOut + ".json"), metadata, StandardCharsets.UTF_8);
        }

        System.out.printf(
                "Generated %,d Java files from %,d base descriptors and %,d supplemental proto files (%,d overrides).%n",
                javaFileCount,
                protocolFiles.size(),
                supplementalProtoFiles.size(),
                overlappingFiles.size());
    }

    private static String sha256(byte[] bytes) throws Exception {
        return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));
    }

    private static String generatorFingerprint(Path root) throws Exception {
        requireDirectory(root, "generator source directory");
        var rows = new TreeSet<String>();
        try (Stream<Path> files = Files.walk(root)) {
            for (Path path : files.filter(Files::isRegularFile)
                    .filter(file -> file.getFileName().toString().endsWith(".java")).toList()) {
                rows.add(root.relativize(path).toString().replace(File.separatorChar, '/')
                        + "\0" + sha256(Files.readAllBytes(path)) + "\n");
            }
        }
        if (rows.isEmpty()) {
            throw new IllegalArgumentException("Generator source directory contains no Java files: " + root);
        }
        return sha256(String.join("", rows).getBytes(StandardCharsets.UTF_8));
    }

    private static String sourceFingerprint(Path protoDir, Path importDir) throws Exception {
        var rows = new TreeSet<String>();
        for (int i = 0; i < 2; i++) {
            Path root = i == 0 ? protoDir : importDir;
            String prefix = i == 0 ? "compat/" : "compat-imports/";
            for (String name : listProtoFiles(root)) {
                rows.add(
                        prefix
                                + name
                                + "\0"
                                + sha256(Files.readAllBytes(root.resolve(name)))
                                + "\n");
            }
        }
        return sha256(String.join("", rows).getBytes(StandardCharsets.UTF_8));
    }

    private static FileDescriptorSet mergeDescriptors(
            FileDescriptorSet base, FileDescriptorSet supplemental) {
        var filesByName = new LinkedHashMap<String, FileDescriptorProto>();
        for (var file : base.getFileList()) {
            filesByName.put(file.getName(), file);
        }
        for (var file : supplemental.getFileList()) {
            filesByName.put(file.getName(), file);
        }

        var result = FileDescriptorSet.newBuilder();
        for (var file : filesByName.values()) {
            result.addFile(file);
        }
        return result.build();
    }

    private static List<String> listProtoFiles(Path directory) throws IOException {
        try (Stream<Path> files = Files.walk(directory)) {
            return files.filter(Files::isRegularFile)
                    .filter(path -> path.getFileName().toString().endsWith(".proto"))
                    .map(directory::relativize)
                    .map(Path::toString)
                    .map(path -> path.replace(File.separatorChar, '/'))
                    .sorted()
                    .toList();
        }
    }

    private static void runSupplementalDescriptorProtoc(
            Path protoc,
            Path supplementalProtoDir,
            Path supplementalImportDir,
            Path supplementalDescriptorSet,
            List<String> supplementalProtoFiles)
            throws IOException, InterruptedException {
        var command = new ArrayList<String>(supplementalProtoFiles.size() + 5);
        command.add(protoc.toString());
        command.add("--proto_path=" + supplementalProtoDir);
        command.add("--proto_path=" + supplementalImportDir);
        command.add("--descriptor_set_out=" + supplementalDescriptorSet);
        command.addAll(supplementalProtoFiles);

        runProtoc(command, "supplemental 7.1 descriptor recovery");
        requireRegularFile(supplementalDescriptorSet, "supplemental descriptor set");
    }

    private static void runDescriptorProtoc(
            Path protoc, Path descriptorSet, Path javaOut, List<String> protocolFiles)
            throws IOException, InterruptedException {
        var command = new ArrayList<String>(protocolFiles.size() + 3);
        command.add(protoc.toString());
        command.add("--descriptor_set_in=" + descriptorSet);
        command.add("--java_out=" + javaOut);
        command.addAll(protocolFiles);

        runProtoc(command, protocolFiles.getFirst() + " .. " + protocolFiles.getLast());
    }

    private static void runProtoc(List<String> command, String description)
            throws IOException, InterruptedException {
        var process = new ProcessBuilder(command).redirectErrorStream(true).start();
        var output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        var exitCode = process.waitFor();
        if (exitCode != 0) {
            throw new IllegalStateException(
                    "protoc failed with exit code "
                            + exitCode
                            + " while generating "
                            + description
                            + (output.isBlank() ? "" : ":\n" + output));
        }
        if (!output.isBlank()) {
            System.out.print(output);
        }
    }

    private static void recreateDirectory(Path directory) throws IOException {
        deleteDirectory(directory);
        Files.createDirectories(directory);
    }

    private static void deleteDirectory(Path directory) throws IOException {
        if (!Files.exists(directory)) {
            return;
        }
        try (Stream<Path> paths = Files.walk(directory)) {
            for (var path : paths.sorted(Comparator.reverseOrder()).toList()) {
                Files.delete(path);
            }
        }
    }

    private static void requireRegularFile(Path path, String description) {
        if (!Files.isRegularFile(path)) {
            throw new IllegalStateException(description + " was not found: " + path);
        }
    }

    private static void requireDirectory(Path path, String description) {
        if (!Files.isDirectory(path)) {
            throw new IllegalStateException(description + " was not found: " + path);
        }
    }
}
