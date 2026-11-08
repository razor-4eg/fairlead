// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "Fairlead",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "Fairlead", targets: ["Fairlead"]),
        .executable(name: "FairleadHelper", targets: ["FairleadHelper"]),
    ],
    targets: [
        .target(
            name: "CProc",
            path: "platform/mac/CProc",
            linkerSettings: [.linkedLibrary("proc")]
        ),
        .target(
            name: "CZlib",
            path: "core",
            sources: ["qcompress.c"],
            publicHeadersPath: "include",
            linkerSettings: [.linkedLibrary("z")]
        ),
        .target(
            name: "FairleadCore",
            path: "platform/mac/FairleadCore",
            dependencies: ["CZlib"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "FairleadHelper",
            path: "platform/mac/Helper",
            dependencies: ["FairleadCore"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "Fairlead",
            path: "platform/mac/App",
            dependencies: ["FairleadCore", "CProc"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "FairleadChecks",
            dependencies: ["FairleadCore"],
            path: "tests/mac",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
