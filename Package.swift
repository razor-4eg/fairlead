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
            linkerSettings: [.linkedLibrary("proc")]
        ),
        .target(
            name: "FairleadCore",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "FairleadHelper",
            dependencies: ["FairleadCore"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "Fairlead",
            dependencies: ["FairleadCore", "CProc"],
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "FairleadChecks",
            dependencies: ["FairleadCore"],
            path: "Tests/FairleadChecks",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
    ]
)
