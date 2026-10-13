import Foundation

public enum FairleadPaths {
    public static let directory = "/var/run/fairlead"
    public static let socket = "/var/run/fairlead/fairlead.sock"
    public static let state = "/var/run/fairlead/state.json"
    public static let helperBinary = "/usr/local/libexec/fairlead-helper"
    public static let log = "/var/log/fairlead-helper.log"
    public static let wireguardRun = "/var/run/wireguard"
}
