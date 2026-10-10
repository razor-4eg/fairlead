// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "Harbor",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "Harbor", targets: ["Harbor"]),
        .executable(name: "HarborHelper", targets: ["HarborHelper"]),
    ],
    targets: [
        .target(
            name: "CProc",
            linkerSettings: [.linkedLibrary("proc")]
        ),
        .target(
            name: "HarborCore",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "HarborHelper",
            dependencies: ["HarborCore"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "Harbor",
            dependencies: ["HarborCore", "CProc"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "HarborChecks",
            dependencies: ["HarborCore"],
            path: "Tests/HarborChecks",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
