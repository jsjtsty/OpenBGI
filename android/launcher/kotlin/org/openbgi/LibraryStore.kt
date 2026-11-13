package org.openbgi

import android.content.Context
import android.content.Intent
import android.os.Environment
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

class LibraryStore(private val context: Context) {
    var installs by mutableStateOf<List<Install>>(emptyList())
        private set
    val statuses = mutableStateMapOf<String, InstallStatus>()
    val probes = mutableStateMapOf<String, ProbeInfo>()
    var crashedInstall by mutableStateOf<String?>(null)

    private val rootDir = File(context.filesDir, "openbgi").apply { mkdirs() }
    private val installsDir = File(rootDir, "Installs").apply { mkdirs() }
    private val indexFile = File(rootDir, "library.json")
    private val ignoredFile = File(rootDir, "ignored.json")
    private val resourcesDir = File(rootDir, "resources").apply { mkdirs() }
    private val ignoredIDs = mutableSetOf<String>()

    private var activeSessionId: String? = null
    private var activeSessionStartMs: Long = 0L

    init {
        ensureResourcesExtracted()
        load()
        detectCrash()
        sync()
    }

    private fun ensureResourcesExtracted() {
        val assets = context.assets
        val stamp = runCatching {
            assets.open("resources.stamp").bufferedReader().use { it.readText().trim() }
        }.getOrNull() ?: return
        val localStampFile = File(resourcesDir, "resources.stamp")
        val currentStamp = runCatching { localStampFile.readText().trim() }.getOrNull()
        if (currentStamp == stamp) return

        val list = runCatching {
            assets.open("resources.lst").bufferedReader().use { it.readLines() }
        }.getOrElse { emptyList() }
        for (rel in list) {
            val trimmed = rel.trim()
            if (trimmed.isEmpty()) continue
            val out = File(resourcesDir, trimmed)
            out.parentFile?.mkdirs()
            assets.open(trimmed).use { input ->
                out.outputStream().use { output -> input.copyTo(output) }
            }
        }
        localStampFile.writeText(stamp)
    }

    private fun installDir(id: String) = File(installsDir, id)
    fun dataDir(id: String) = File(installDir(id), "Data")
    private fun logFile(id: String) = File(installDir(id), "last-run.log")
    private fun flagFile(id: String) = File(installDir(id), "running.flag")

    fun install(id: String): Install? = installs.firstOrNull { it.id == id }

    private fun load() {
        if (ignoredFile.isFile) {
            runCatching {
                val arr = JSONArray(ignoredFile.readText())
                for (i in 0 until arr.length()) ignoredIDs.add(arr.getString(i))
            }
        }
        if (!indexFile.isFile) return
        runCatching {
            val arr = JSONArray(indexFile.readText())
            val list = ArrayList<Install>(arr.length())
            for (i in 0 until arr.length()) {
                val o = arr.getJSONObject(i)
                val optObj = o.optJSONObject("options")
                val drvName = optObj?.optString("driver", "AUTO") ?: "AUTO"
                val drv = GpuDriver.entries.firstOrNull { it.name == drvName } ?: GpuDriver.AUTO
                list.add(
                    Install(
                        id = o.getString("id"),
                        gameID = o.optString("gameID", "bgi"),
                        name = o.getString("name"),
                        path = o.getString("path"),
                        options = PlayOptions(
                            driver = drv,
                            entryArc = optObj?.optString("entryArc", "autoload.arc") ?: "autoload.arc",
                            entryName = optObj?.optString("entryName", "ipl._bp") ?: "ipl._bp"
                        ),
                        lastPlayedMs = o.optLong("lastPlayedMs", 0L),
                        playSeconds = o.optDouble("playSeconds", 0.0)
                    )
                )
            }
            installs = list
        }
    }

    private fun save() {
        val arr = JSONArray()
        for (item in installs) {
            val opt = JSONObject().apply {
                put("driver", item.options.driver.name)
                put("entryArc", item.options.entryArc)
                put("entryName", item.options.entryName)
            }
            val o = JSONObject().apply {
                put("id", item.id)
                put("gameID", item.gameID)
                put("name", item.name)
                put("path", item.path)
                put("options", opt)
                put("lastPlayedMs", item.lastPlayedMs)
                put("playSeconds", item.playSeconds)
            }
            arr.put(o)
        }
        indexFile.writeText(arr.toString())
    }

    private fun saveIgnored() {
        val arr = JSONArray()
        for (id in ignoredIDs) arr.put(id)
        ignoredFile.writeText(arr.toString())
    }

    fun update(id: String, change: (Install) -> Install) {
        val idx = installs.indexOfFirst { it.id == id }
        if (idx < 0) return
        val copy = installs.toMutableList()
        copy[idx] = change(copy[idx])
        installs = copy
        save()
    }

    fun rename(id: String, newName: String) {
        val trimmed = newName.trim()
        if (trimmed.isNotEmpty()) update(id) { it.copy(name = trimmed) }
    }

    fun remove(id: String, deleteSaves: Boolean) {
        ignoredIDs.add(id)
        saveIgnored()
        if (deleteSaves) {
            installDir(id).deleteRecursively()
        }
        installs = installs.filterNot { it.id == id }
        statuses.remove(id)
        probes.remove(id)
        save()
    }

    private fun stableID(key: String): String {
        var h = -0x340d631b7bdddcdbL
        for (b in key.toByteArray(Charsets.UTF_8)) {
            h = (h xor (b.toLong() and 0xffL)) * 0x100000001b3L
        }
        return java.lang.Long.toHexString(h)
    }

    private fun discoverFolders(base: File, maxDepth: Int): List<File> {
        val found = mutableListOf<File>()
        fun walk(dir: File, depth: Int) {
            if (!dir.isDirectory) return
            if (Engine.detect(dir.absolutePath) != null) {
                found.add(dir)
                return
            }
            if (depth >= maxDepth) return
            val children = dir.listFiles() ?: return
            for (child in children) {
                if (child.isDirectory && !child.name.startsWith(".")) {
                    walk(child, depth + 1)
                }
            }
        }
        walk(base, 0)
        return found
    }

    fun sync() {
        val roots = mutableListOf<File>()
        val extRoot = Environment.getExternalStorageDirectory()
        if (extRoot != null) {
            val shared = File(extRoot, "OpenBGI")
            if (!shared.exists()) runCatching { shared.mkdirs() }
            roots.add(shared)
        }
        context.getExternalFilesDir(null)?.let { roots.add(it) }

        val seenPaths = mutableSetOf<String>()
        val mutable = installs.toMutableList()
        for (r in roots) {
            for (dir in discoverFolders(r, 2)) {
                val p = dir.canonicalPath
                if (!seenPaths.add(p)) continue
                val id = stableID("path:$p")
                if (!ignoredIDs.contains(id) && mutable.none { it.id == id }) {
                    val det = Engine.detect(p) ?: continue
                    mutable.add(Install(id = id, gameID = det.id, name = dir.name, path = p))
                }
            }
        }
        installs = mutable
        save()

        for (item in installs) {
            val dir = File(item.path)
            if (!dir.isDirectory) {
                statuses[item.id] = InstallStatus.Missing
                probes.remove(item.id)
                continue
            }
            val p = Engine.probe(item.path)
            probes[item.id] = p
            statuses[item.id] = when {
                !p.ok -> InstallStatus.Incomplete(p.missing.ifEmpty { p.message })
                !p.complete -> InstallStatus.Incomplete(p.missing)
                else -> InstallStatus.Ready
            }
        }
    }

    fun dataBytes(id: String): Long {
        val dir = dataDir(id)
        if (!dir.exists()) return 0L
        return dir.walkTopDown().filter { it.isFile }.sumOf { it.length() }
    }

    fun clearData(id: String) {
        dataDir(id).deleteRecursively()
        dataDir(id).mkdirs()
    }

    fun logText(id: String): String {
        val f = logFile(id)
        if (!f.isFile) return ""
        val bytes = f.readBytes()
        val start = maxOf(0, bytes.size - 24_000)
        return String(bytes, start, bytes.size - start, Charsets.UTF_8)
    }

    fun clearLog(id: String) {
        logFile(id).delete()
    }

    private fun detectCrash() {
        for (item in installs) {
            val f = flagFile(item.id)
            if (f.exists()) {
                crashedInstall = item.name
                f.delete()
            }
        }
    }

    fun onResumeLauncher() {
        val activeId = activeSessionId
        if (activeId != null) {
            val elapsed = (System.currentTimeMillis() - activeSessionStartMs) / 1000.0
            if (elapsed > 0) {
                update(activeId) { it.copy(playSeconds = it.playSeconds + elapsed) }
            }
            activeSessionId = null
        }
        detectCrash()
        sync()
    }

    fun canPlay(id: String): Boolean = probes[id]?.ok == true

    fun play(item: Install) {
        if (!canPlay(item.id)) return
        dataDir(item.id).mkdirs()
        installDir(item.id).mkdirs()
        logFile(item.id).writeText("")

        val args = mutableListOf(
            item.path,
            "--launcher",
            "--window",
            "--resources-dir", resourcesDir.absolutePath,
            "--data-dir", dataDir(item.id).absolutePath,
            "--log-file", logFile(item.id).absolutePath,
            "--entry", item.options.entryArc, item.options.entryName
        )
        item.options.driver.cliValue?.let { drv ->
            args.add("--driver")
            args.add(drv)
        }

        val now = System.currentTimeMillis()
        update(item.id) { it.copy(lastPlayedMs = now) }
        activeSessionId = item.id
        activeSessionStartMs = now

        val intent = Intent(context, OpenBGIActivity::class.java).apply {
            putExtra("openbgi.args", args.toTypedArray())
            putExtra("openbgi.flag", flagFile(item.id).absolutePath)
        }
        context.startActivity(intent)
    }
}
