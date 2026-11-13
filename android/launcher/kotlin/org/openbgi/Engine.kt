package org.openbgi

object Engine {
    init {
        System.loadLibrary("SDL3")
        System.loadLibrary("main")
    }

    @JvmStatic
    external fun nativeVersion(): String

    @JvmStatic
    external fun nativeDetect(dir: String): Array<String>?

    @JvmStatic
    external fun nativeProbe(dir: String): Array<String>

    val version: String
        get() = nativeVersion()

    fun detect(dir: String): DetectedGame? {
        val res = nativeDetect(dir) ?: return null
        if (res.size < 2) return null
        return DetectedGame(id = res[0], name = res[1])
    }

    fun probe(dir: String): ProbeInfo {
        val r = nativeProbe(dir)
        return ProbeInfo(
            ok = r.getOrNull(0) == "1",
            complete = r.getOrNull(1) == "1",
            arcsFound = r.getOrNull(2)?.toIntOrNull() ?: 0,
            hasScPatch = r.getOrNull(3) == "1",
            gameID = r.getOrNull(4) ?: "",
            gameName = r.getOrNull(5) ?: "",
            missing = r.getOrNull(6) ?: "",
            message = r.getOrNull(7) ?: ""
        )
    }
}
