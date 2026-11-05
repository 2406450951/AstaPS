package emu.grasscutter.tools.protocol;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Stream;

/**
 * Regenerates the checked-in protocol Java from the split 7.2 sources.
 *
 * <p>Usage: {@code ProtocolJavaGenerator <protoc> <proto-dir> <java-out>}. Every {@code .proto}
 * in the proto directory is compiled with protoc, in batches so the command line stays short on
 * Windows, into a fresh copy of the output directory.
 */
public final class ProtocolJavaGenerator {
    private static final int FILES_PER_PROTOC_INVOCATION = 256;

    private ProtocolJavaGenerator() {}

    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException(
                    "usage: ProtocolJavaGenerator <protoc> <proto-dir> <java-out>");
        }
        var protoc = Path.of(args[0]).toAbsolutePath().normalize();
        var protoDir = Path.of(args[1]).toAbsolutePath().normalize();
        var javaOut = Path.of(args[2]).toAbsolutePath().normalize();
        if (!Files.isRegularFile(protoc)) {
            throw new IllegalStateException("protoc executable was not found: " + protoc);
        }
        if (!Files.isDirectory(protoDir)) {
            throw new IllegalStateException("proto directory was not found: " + protoDir);
        }

        List<String> protoFiles;
        try (Stream<Path> files = Files.list(protoDir)) {
            protoFiles = files.map(path -> path.getFileName().toString())
                    .filter(name -> name.endsWith(".proto"))
                    .sorted()
                    .toList();
        }
        if (protoFiles.isEmpty()) {
            throw new IllegalStateException("no .proto files in " + protoDir);
        }

        deleteDirectory(javaOut);
        Files.createDirectories(javaOut);
        for (int offset = 0; offset < protoFiles.size(); offset += FILES_PER_PROTOC_INVOCATION) {
            var command = new ArrayList<String>();
            command.add(protoc.toString());
            command.add("--proto_path=" + protoDir);
            command.add("--java_out=" + javaOut);
            command.addAll(protoFiles.subList(
                    offset, Math.min(offset + FILES_PER_PROTOC_INVOCATION, protoFiles.size())));
            var process = new ProcessBuilder(command).redirectErrorStream(true).start();
            var output = new String(process.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
            if (process.waitFor() != 0) {
                throw new IllegalStateException("protoc failed:\n" + output);
            }
        }

        long javaFiles;
        try (Stream<Path> files = Files.walk(javaOut)) {
            javaFiles = files.filter(path -> path.toString().endsWith(".java")).count();
        }
        if (javaFiles != protoFiles.size()) {
            throw new IllegalStateException(
                    "protoc generated " + javaFiles + " Java files from " + protoFiles.size()
                            + " .proto files");
        }
        System.out.println("Generated " + javaFiles + " protocol Java files into " + javaOut);
    }

    private static void deleteDirectory(Path directory) throws IOException {
        if (!Files.exists(directory)) return;
        try (Stream<Path> paths = Files.walk(directory)) {
            for (var path : paths.sorted(Comparator.reverseOrder()).toList()) {
                Files.delete(path);
            }
        }
    }
}
