# SuperMoTo — Audio Unit validation (macOS tester instructions)

This file tells a macOS tester how to validate the SuperMoTo Audio Unit with
Apple's `auval` tool and what to send back. It is generated from the
`juce_add_plugin` block of `CMakeLists.txt`, so it stays in sync with the
build.

## 1. Plugin to validate

| Product | AU type | Manufacturer code | Plugin code | Command |
|---|---|---|---|---|
| SuperMoTo | Audio effect (`aufx`) | `FXME` | `SMTO` | `auval -v aufx SMTO FXME` |

The type follows from the build settings: not a MIDI effect, not an
instrument, and no MIDI input, so `kAudioUnitType_Effect` (`aufx`), which is
what `AU_MAIN_TYPE` declares.

## 2. Before running

1. Copy the built bundle `SuperMoTo.component` into one of:
   - `~/Library/Audio/Plug-Ins/Components/` (this user only), or
   - `/Library/Audio/Plug-Ins/Components/` (all users, needs admin rights).
2. The bundle is not notarised, so macOS quarantines a downloaded copy and
   every host silently skips it. Clear that first:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/SuperMoTo.component
   ```
3. Flush the AU cache so a stale validation result is not reused:
   ```
   killall -9 AudioComponentRegistrar
   ```

## 3. Run the validation

```
auval -v aufx SMTO FXME
```

To keep the whole output for a report:

```
auval -v aufx SMTO FXME > ~/Desktop/supermoto-auval.txt 2>&1
```

## 4. What a passing run looks like

The run ends with

```
AU VALIDATION SUCCEEDED.
```

and the command exits with status 0 (`echo $?` prints `0` right after it).

Along the way, the "Reported Channel Capabilities" section prints the
input/output channel counts the plugin declares. SuperMoTo declares a fixed
32-in / 32-out discrete bus on purpose, so expect `32, 32` there rather than
the usual `-1, -1` or `2, 2` of a stereo effect. That is the intended
configuration and not a fault.

## 5. What to send back if it fails

Please include all of the following:

1. The **full terminal output** of the `auval` command (the file from
   section 3, not just the last lines).
2. Any **crash report** written while it ran, from
   `~/Library/Logs/DiagnosticReports/`, with a name starting `auvaltool` or
   `auval`. In Finder: Go → Go to Folder → paste that path.
3. The macOS version and the CPU architecture:
   ```
   sw_vers -productVersion
   uname -m
   ```
4. Which copy of the bundle was tested (user or system folder) and where it
   came from (locally built or downloaded release).

## 6. If it hangs

`auval` can stop making progress instead of failing. Give it about 60
seconds, then:

1. Press `Ctrl+C` in the terminal to stop it.
2. Capture what it was doing, in a second terminal, while it is still
   running if possible:
   ```
   sample auval 10 -file ~/Desktop/supermoto-auval-sample.txt
   ```
   or, for a full system picture:
   ```
   sudo spindump auval 10 -file ~/Desktop/supermoto-auval-spindump.txt
   ```
3. Send that file together with the partial terminal output.

## 7. Note on Logic Pro

Passing `auval` means the component is a well-formed Audio Unit. It does not
mean Logic Pro can host SuperMoTo usefully. The plugin needs a discrete
32-in / 32-out insert on the monitoring path, which Logic's fixed surround
formats do not offer. REAPER on macOS is the tested host. Testing in Logic is
still welcome, but an inability to insert the plugin there is expected rather
than a bug to chase.
