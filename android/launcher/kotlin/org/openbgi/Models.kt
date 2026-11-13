package org.openbgi

enum class GpuDriver(val cliValue: String?) {
    AUTO(null),
    VULKAN("vulkan")
}

data class PlayOptions(
    val driver: GpuDriver = GpuDriver.AUTO,
    val entryArc: String = "autoload.arc",
    val entryName: String = "ipl._bp"
)

data class Install(
    val id: String,
    val gameID: String,
    val name: String,
    val path: String,
    val options: PlayOptions = PlayOptions(),
    val lastPlayedMs: Long = 0L,
    val playSeconds: Double = 0.0
)

sealed class InstallStatus {
    data object Ready : InstallStatus()
    data class Incomplete(val missing: String) : InstallStatus()
    data object Missing : InstallStatus()
}

data class ProbeInfo(
    val ok: Boolean,
    val complete: Boolean,
    val arcsFound: Int,
    val hasScPatch: Boolean,
    val gameID: String,
    val gameName: String,
    val missing: String,
    val message: String
)

data class DetectedGame(
    val id: String,
    val name: String
)
