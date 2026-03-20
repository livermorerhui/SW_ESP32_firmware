# Android Demo Toolchain Guardrails

This document records the pinned toolchain for `tools/android_demo` and how to avoid IDE-triggered drift.

## Pinned Stable Versions

- AGP: `8.9.1`
- Gradle Wrapper: `8.11.1`
- Kotlin: `1.9.24`
- Gradle JVM: Android Studio embedded JBR
  - `org.gradle.java.home=/Applications/Android Studio.app/Contents/jbr/Contents/Home`

## Why Versions Are Pinned

This project has validated behavior and protocol/demo logic against the versions above.
Auto-upgrading from Android Studio prompts can introduce incompatible plugin combinations and Gradle runtime issues.

Do not auto-accept IDE upgrade prompts for:

- AGP major/minor bumps
- Gradle wrapper bumps
- Kotlin plugin bumps
- Gradle JVM source changes

## Validation Commands

Run from `tools/android_demo`:

```bash
./gradlew --stop
./gradlew -version
./gradlew projects
./gradlew :sonicwave-protocol:test
./gradlew :app-demo:assembleDebug
```

## Common Failure Signatures

1. BaseExtension cast crash
- Symptom: ClassCastException involving `BaseExtension` / Android extension access.
- Typical cause: AGP/Kotlin/Gradle plugin API mismatch after partial upgrades.

2. Duplicate kotlin extension
- Symptom: errors indicating duplicate Kotlin extension registration.
- Typical cause: conflicting Kotlin plugin application pattern after IDE refactor.

3. Missing jlink / VSCode JRE path
- Symptom: `jlink executable ... does not exist` and path points to VSCode Java extension or Homebrew JVM unexpectedly.
- Typical cause: Gradle daemon/JVM drift away from Android Studio JBR.
- Fix: keep `org.gradle.java.home` pinned in `gradle.properties` and rerun with clean daemon.
