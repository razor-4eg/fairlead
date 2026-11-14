// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "Fairlead",
    platforms: [.macOS(.v15)],
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
            name: "FairleadC",
            path: "core",
            exclude: ["abi.c"],
            publicHeadersPath: "include",
            cSettings: [.headerSearchPath(".")],
            linkerSettings: [.linkedLibrary("z")]
        ),
        .target(
            name: "FairleadCore",
            dependencies: ["FairleadC"],
            path: "platform/mac/FairleadCore",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "FairleadHelper",
            dependencies: ["FairleadCore"],
            path: "platform/mac/Helper",
            swiftSettings: [.swiftLanguageMode(.v5)]
        ),
        .executableTarget(
            name: "Fairlead",
            dependencies: ["FairleadCore", "CProc"],
            path: "platform/mac/App",
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
