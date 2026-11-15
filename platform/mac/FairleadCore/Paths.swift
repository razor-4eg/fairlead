import Foundation

public enum FairleadPaths {
    public static let directory = "/var/run/fairlead"
    public static let socket = "/var/run/fairlead/fairlead.sock"
    public static let state = "/var/run/fairlead/state.json"
    public static let helperBinary = "/usr/local/libexec/fairlead-helper"
    public static let log = "/var/log/fairlead-helper.log"
    /// mihomo working directory: config.yaml, mihomo.log, geo data. Root only.
    public static let mihomoDirectory = "/var/run/fairlead/mihomo"
    public static let mihomoPID = "/var/run/fairlead/mihomo.pid"
}
