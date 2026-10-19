plugins {
    `java-library`
}

tasks.withType<JavaCompile>().configureEach {
    options.release.set(17)
}

// Plain JVM module: no Android dependencies, so it is unit-tested without an SDK.
// org.json is part of the Android platform, so it is compile-only here.
dependencies {
    compileOnly("org.json:json:20250517")
    testImplementation("org.json:json:20250517")
    testImplementation(platform("org.junit:junit-bom:5.11.4"))
    testImplementation("org.junit.jupiter:junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

tasks.test {
    useJUnitPlatform()
}
