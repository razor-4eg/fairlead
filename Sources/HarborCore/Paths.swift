import Foundation

public enum HarborPaths {
    public static let directory = "/var/run/harbor"
    public static let socket = "/var/run/harbor/harbor.sock"
    public static let state = "/var/run/harbor/state.json"
    public static let helperBinary = "/usr/local/libexec/harbor-helper"
    public static let log = "/var/log/harbor-helper.log"
    public static let wireguardRun = "/var/run/wireguard"
}
