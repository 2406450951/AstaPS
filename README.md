# AstaPS

English · [简体中文](README_zh-CN.md) · [繁體中文](README_zh-TW.md)

A private server for Genshin Impact **7.1.0**, built on Grasscutter.

> This is a research and preservation project. It is not affiliated with, endorsed by, or connected to HoYoverse / miHoYo in any way, and it is not for commercial use.

If you can fix a bug, please help me.

## What it is

- **Up-to-date content.** Monster and gadget spawn data current with 7.1.0, Spiral Abyss rotations, domains, the artifact shop, battle pass, and the rest of the live-service surface.
- **Built to survive a bad day.** Database writes are split across four bounded pools that apply backpressure instead of dropping a player's progress; one world throwing during a tick no longer stops all the others.
- **Visible when it is unwell.** A status readout logs CPU, memory, GC and every thread pool's queue depth on an interval, and `/api/status` serves the same figures over HTTP.
- **English throughout.** Source, comments, commit messages and command output.

## Requirements

| | |
|---|---|
| Java | **JDK 21** to build and **Java 21** to run. AstaPS uses Java 21 APIs such as virtual threads. |
| MongoDB | Community Server. Must be running before the server starts. |
| Game client | Genshin Impact 7.1.0. The official client checks the region's signature, so it needs a client patch to connect to a private server, such as [hk4e-patch-universal](https://github.com/MeChen618/patch). AstaPS does not ship one. |
| Resources | A 7.1.0 resource pack, extracted to `resources/` in the server directory. If you don't have Resources, you can download it [here](https://github.com/MeChen618/AstaPS-Resource). |

## Building

Check that both Java commands resolve to version 21 before building:

```
java -version
javac -version
```

Then build with:

```
./gradlew jar -PskipHandbook=1
```

`grasscutter-7.1.0.jar` lands in the project root. Drop `-PskipHandbook=1` to build the in-game handbook as well; that step needs NodeJS and fails without it.

On Windows use `.\gradlew.bat`, or run `gradlew-jar.bat`.

### Faster local builds

The wrapper uses **Gradle 9.8.1**. The build cache and configuration cache are enabled in `gradle.properties`; unchanged protocol generation and compilation outputs can be reused across builds and Git worktrees.

To compile without packaging a JAR:

```sh
./gradlew classes -PskipHandbook=1
```

For a quicker **local-test** fat JAR, disable ZIP compression:

```sh
./gradlew jar -PskipHandbook=1 -PfastJar
```

On Windows, use `.\gradlew.bat` in place of `./gradlew`. Both modes produce `grasscutter-7.1.0.jar` in the project root. The fast mode keeps the same JAR entries but makes the file much larger (roughly 230 MB instead of 85 MB in CI); use the default compressed build for distribution.

## Running

1. Start MongoDB.
2. Put a 7.1.0 resource pack in `resources/`.
3. Run the jar once. It writes a `config.json` and stops if anything essential is missing.
4. Start it again. The dispatch server listens on `8088` and the game server on `22101` by default.
5. Point the client at the dispatch server. A proxy such as Fiddler or mitmproxy will do it, as will a client patch.

### Accounts

There is no registration page. An account is created either way:

- **From the console.** `account create <username> [password] [@UID]` (password and UID optional).
- **At sign-in.** Signing in with a name nobody holds registers it. With `account.useIntegrationPassword` on, put `name&&password` in the username box and leave the password box alone — useful when proxying a bunch of clients at once and you don't want to set up a user for each.

Provided passwords are BCrypt-hashed. An account created without a password has no password check until one is set. New accounts receive only configured default permissions, never an automatic `*` administrator grant. The console needs `server.game.enableConsole` set to `true`.

### Web console (opt-in)

The HTTP web console at `/console` can execute server-console commands. It is **disabled by
default** and has **no built-in password**. To enable it, edit the existing `config.json`:

```json
{
  "server": {
    "http": {
      "webConsole": {
        "enabled": true,
        "password": "replace-with-a-long-unique-secret"
      }
    }
  }
}
```

This is an excerpt: preserve all the other settings in your generated `config.json`.
If `enabled` is true but `password` is missing or blank, the web-console routes are not
registered and startup logs an error. Older installations that relied on the former built-in
password must set their own password explicitly after upgrading.

The console password and session token are transmitted over HTTP unless you provide
transport encryption. Keep the HTTP port behind a firewall or trusted TLS reverse proxy;
for a single-machine setup, set `server.http.bindAddress` to `127.0.0.1`. Never expose
this command-execution interface directly to the public Internet.

## Commands

In-game commands use `/`; the server console omits it. Use `help` to list commands, or `help <command> [subcommand...]` for syntax and aliases. The console supports Tab completion; invalid arguments show usage.

Player selectors: `@UID`, `username@`, or `username@UID`. Account management is console-only.

```text
help teleport pos
give 202 --amount 3 @10001
```

See the [CLI migration guide](docs/cli-picocli-migration.md) for details and the [command API v5 guide](docs/plugin-command-api-v5.md) for plugins.

## TPS shooting (7.1)

Snezhnaya's third-person shooter mode works: guns and grenades worn next to an avatar's normal weapon, aimed and fired in TPS domains. The commands need the `player.tps` and `player.enterdungeon` permissions.

**1. Get the weapons**

```
/tps give
/tps give 224001
/tps accessory
```

The first command grants all eight TPS weapons (224001–224008); the second grants only weapon 224001. The third unlocks accessories for owned weapons.

**2. Play a TPS domain**

```
/dungeon 10955
/dungeon 10953
/dungeon 10960
```

Each line is a separate command. Dungeon 10955 is the shooting range; 10953 and 10960–10964 are Emerged Grey Field stages.

Inside, your team is swapped for the TPS Traveler (matching your Traveler, level 20), wearing your TPS loadout, or 224001 the first time. Weapons you switch to there are kept as your loadout. Leaving the domain restores your team.

**3. Outside a domain**

Any avatar can wear TPS weapons, which is handy for trying them out:

```
/tps wear 224001 224004
/tps refill
```

The first command equips a rifle and a grenade (at most two guns and one grenade); the second refills ammunition.

Ammunition handling is still partly experimental. See [docs/tps/README.md](docs/tps/README.md) for what the server does, the `/tps ammo` switches, and what is not settled yet.

## Licence

Released under the **GNU General Public License v3.0**. See [`LICENSE`](LICENSE).

`LICENSE-ClassGraph.txt` is not this project's licence. ClassGraph is an MIT-licensed dependency whose compiled classes ship inside `grasscutter-7.1.0.jar`, and MIT asks only that its notice travels with them.

## Credits

This server is based on **Grasscutter**. Reference projects: **LunaGC**, **HunkyMeow**.

The client patch linked under Requirements, [hk4e-patch-universal](https://github.com/MeChen618/patch), is a copy of [capyb2222/animegamepatch](https://github.com/capyb2222/animegamepatch), maintained by **capyb2222**, based on [xeondev](https://git.xeondev.com/reversedrooms/hk4e-patch)'s original hk4e-patch and [oureveryday](https://github.com/oureveryday/)'s original hk4e-patch-universal. It is a separate project under its own GPL-3.0 licence.

The import commit at the root of this repository credits by name the authors whose work it carries.

## Proto Sources

Protocol definitions sourced from [genshin-protocol](https://gitlab.com/kitkat-multiverse/genshin-protocol).
