package org.openbgi.ui

import android.os.Build
import android.text.format.Formatter
import androidx.compose.animation.AnimatedContentTransitionScope
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.rounded.Archive
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.Edit
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.History
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.MoreVert
import androidx.compose.material.icons.rounded.PlayArrow
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Schedule
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material.icons.rounded.Tag
import androidx.compose.material.icons.rounded.Terminal
import androidx.compose.material.icons.rounded.Translate
import androidx.compose.material.icons.rounded.WarningAmber
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ElevatedCard
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeTopAppBar
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import org.openbgi.Engine
import org.openbgi.GpuDriver
import org.openbgi.Install
import org.openbgi.InstallStatus
import org.openbgi.LibraryStore
import org.openbgi.R
import java.text.DateFormat
import java.util.Date

@Composable
fun OpenBGITheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val context = LocalContext.current
    val scheme = when {
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && dark -> dynamicDarkColorScheme(context)
        Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !dark -> dynamicLightColorScheme(context)
        dark -> darkColorScheme()
        else -> lightColorScheme()
    }
    MaterialTheme(colorScheme = scheme, content = content)
}

@Composable
fun LauncherApp(
    store: LibraryStore,
    hasStorageAccess: Boolean,
    onRequestStorageAccess: () -> Unit
) {
    OpenBGITheme {
        val nav = rememberNavController()
        var showAbout by remember { mutableStateOf(false) }

        if (store.crashedInstall != null) {
            AlertDialog(
                onDismissRequest = { store.crashedInstall = null },
                icon = { Icon(Icons.Rounded.WarningAmber, contentDescription = null) },
                title = { Text(stringResource(R.string.crashed_title)) },
                text = { Text(stringResource(R.string.crashed_msg, store.crashedInstall ?: "")) },
                confirmButton = {
                    TextButton(onClick = { store.crashedInstall = null }) {
                        Text(stringResource(R.string.ok))
                    }
                }
            )
        }

        if (showAbout) {
            AlertDialog(
                onDismissRequest = { showAbout = false },
                icon = { Icon(Icons.Rounded.Info, contentDescription = null) },
                title = { Text(stringResource(R.string.app_name)) },
                text = {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        Text(
                            "${stringResource(R.string.kernel_version)}: ${Engine.version}",
                            style = MaterialTheme.typography.labelLarge
                        )
                        Text(
                            stringResource(R.string.about_desc),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                },
                confirmButton = {
                    TextButton(onClick = { showAbout = false }) {
                        Text(stringResource(R.string.ok))
                    }
                }
            )
        }

        NavHost(
            navController = nav,
            startDestination = "library",
            enterTransition = {
                slideIntoContainer(AnimatedContentTransitionScope.SlideDirection.Start, tween(260))
            },
            exitTransition = {
                slideOutOfContainer(AnimatedContentTransitionScope.SlideDirection.Start, tween(260))
            },
            popEnterTransition = {
                slideIntoContainer(AnimatedContentTransitionScope.SlideDirection.End, tween(260))
            },
            popExitTransition = {
                slideOutOfContainer(AnimatedContentTransitionScope.SlideDirection.End, tween(260))
            }
        ) {
            composable("library") {
                LibraryScreen(
                    store = store,
                    hasStorageAccess = hasStorageAccess,
                    onRequestStorageAccess = onRequestStorageAccess,
                    onOpenDetail = { id -> nav.navigate("detail/$id") },
                    onOpenAbout = { showAbout = true }
                )
            }
            composable("detail/{id}") { backStackEntry ->
                val id = backStackEntry.arguments?.getString("id") ?: return@composable
                val item = store.install(id)
                if (item == null) {
                    nav.popBackStack()
                } else {
                    DetailScreen(
                        store = store,
                        install = item,
                        onBack = { nav.popBackStack() }
                    )
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun LibraryScreen(
    store: LibraryStore,
    hasStorageAccess: Boolean,
    onRequestStorageAccess: () -> Unit,
    onOpenDetail: (String) -> Unit,
    onOpenAbout: () -> Unit
) {
    var search by remember { mutableStateOf("") }
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior()

    val filtered = remember(store.installs, search) {
        val q = search.trim()
        if (q.isEmpty()) store.installs
        else store.installs.filter {
            it.name.contains(q, ignoreCase = true) || it.gameID.contains(q, ignoreCase = true)
        }
    }

    Scaffold(
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            LargeTopAppBar(
                title = {
                    Text(
                        stringResource(R.string.app_name),
                        fontWeight = FontWeight.SemiBold
                    )
                },
                actions = {
                    IconButton(onClick = { store.sync() }) {
                        Icon(Icons.Rounded.Refresh, contentDescription = stringResource(R.string.rescan))
                    }
                    IconButton(onClick = onOpenAbout) {
                        Icon(Icons.Rounded.Info, contentDescription = stringResource(R.string.about))
                    }
                },
                scrollBehavior = scrollBehavior
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            if (!hasStorageAccess) {
                ElevatedCard(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    colors = CardDefaults.elevatedCardColors(
                        containerColor = MaterialTheme.colorScheme.secondaryContainer,
                        contentColor = MaterialTheme.colorScheme.onSecondaryContainer
                    )
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Icon(Icons.Rounded.FolderOpen, contentDescription = null)
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Text(
                                stringResource(R.string.storage_needed),
                                style = MaterialTheme.typography.bodyMedium
                            )
                            Button(onClick = onRequestStorageAccess) {
                                Text(stringResource(R.string.grant_storage))
                            }
                        }
                    }
                }
            }

            if (store.installs.isEmpty()) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(32.dp),
                    contentAlignment = Alignment.Center
                ) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(14.dp)
                    ) {
                        Surface(
                            modifier = Modifier.size(80.dp),
                            shape = CircleShape,
                            color = MaterialTheme.colorScheme.primaryContainer
                        ) {
                            Box(contentAlignment = Alignment.Center) {
                                Icon(
                                    Icons.Rounded.FolderOpen,
                                    contentDescription = null,
                                    modifier = Modifier.size(38.dp),
                                    tint = MaterialTheme.colorScheme.onPrimaryContainer
                                )
                            }
                        }
                        Text(
                            stringResource(R.string.no_games_title),
                            style = MaterialTheme.typography.titleLarge,
                            textAlign = TextAlign.Center
                        )
                        Text(
                            stringResource(R.string.no_games_desc),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            textAlign = TextAlign.Center
                        )
                    }
                }
            } else {
                if (store.installs.size > 1 || search.isNotEmpty()) {
                    M3SearchBar(
                        query = search,
                        onQueryChange = { search = it },
                        placeholder = stringResource(R.string.search_games),
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 6.dp)
                    )
                }

                LazyColumn(
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(16.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    items(filtered, key = { it.id }) { item ->
                        GameRowCard(
                            install = item,
                            status = store.statuses[item.id] ?: InstallStatus.Missing,
                            hasScPatch = store.probes[item.id]?.hasScPatch == true,
                            canPlay = store.canPlay(item.id),
                            onPlay = { store.play(item) },
                            onClick = { onOpenDetail(item.id) }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun M3SearchBar(
    query: String,
    onQueryChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier.height(50.dp),
        shape = CircleShape,
        color = MaterialTheme.colorScheme.surfaceContainerHigh
    ) {
        Row(
            modifier = Modifier
                .fillMaxSize()
                .padding(horizontal = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Icon(
                Icons.Rounded.Search,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
                if (query.isEmpty()) {
                    Text(
                        placeholder,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                BasicTextField(
                    value = query,
                    onValueChange = onQueryChange,
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodyLarge.copy(
                        color = MaterialTheme.colorScheme.onSurface
                    ),
                    cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                    modifier = Modifier.fillMaxWidth()
                )
            }
            if (query.isNotEmpty()) {
                IconButton(onClick = { onQueryChange("") }, modifier = Modifier.size(28.dp)) {
                    Icon(
                        Icons.Rounded.Close,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        }
    }
}

@Composable
private fun GameRowCard(
    install: Install,
    status: InstallStatus,
    hasScPatch: Boolean,
    canPlay: Boolean,
    onPlay: () -> Unit,
    onClick: () -> Unit
) {
    ElevatedCard(
        onClick = onClick,
        shape = RoundedCornerShape(20.dp),
        modifier = Modifier.fillMaxWidth()
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            GameArtworkBadge(size = 52)

            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                Text(
                    install.name,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Medium,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    val dotColor = when (status) {
                        is InstallStatus.Ready -> Color(0xFF2E9E5B)
                        is InstallStatus.Incomplete -> Color(0xFFE08E26)
                        is InstallStatus.Missing -> MaterialTheme.colorScheme.error
                    }
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .clip(CircleShape)
                            .background(dotColor)
                    )
                    val statusLabel = when (status) {
                        is InstallStatus.Ready -> stringResource(R.string.ready)
                        is InstallStatus.Incomplete -> stringResource(R.string.incomplete, status.missing)
                        is InstallStatus.Missing -> stringResource(R.string.missing)
                    }
                    Text(
                        statusLabel,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    if (hasScPatch) {
                        Surface(
                            color = MaterialTheme.colorScheme.tertiaryContainer,
                            shape = RoundedCornerShape(6.dp)
                        ) {
                            Text(
                                "SC",
                                modifier = Modifier.padding(horizontal = 6.dp, vertical = 1.dp),
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onTertiaryContainer
                            )
                        }
                    }
                }
            }

            if (canPlay) {
                FilledTonalIconButton(onClick = onPlay) {
                    Icon(Icons.Rounded.PlayArrow, contentDescription = stringResource(R.string.play))
                }
            }
        }
    }
}

@Composable
private fun GameArtworkBadge(size: Int) {
    Box(
        modifier = Modifier
            .size(size.dp)
            .clip(RoundedCornerShape((size * 0.26f).dp))
            .background(
                Brush.linearGradient(
                    listOf(Color(0xFF0E254D), Color(0xFF1A5B82))
                )
            ),
        contentAlignment = Alignment.Center
    ) {
        Icon(
            Icons.Rounded.SportsEsports,
            contentDescription = null,
            tint = Color.White,
            modifier = Modifier.size((size * 0.54f).dp)
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun DetailScreen(
    store: LibraryStore,
    install: Install,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val probe = store.probes[install.id]
    val status = store.statuses[install.id] ?: InstallStatus.Missing

    var showMenu by remember { mutableStateOf(false) }
    var showRename by remember { mutableStateOf(false) }
    var renameText by remember { mutableStateOf(install.name) }
    var showRemove by remember { mutableStateOf(false) }
    var showClearSaves by remember { mutableStateOf(false) }
    var showLog by remember { mutableStateOf(false) }
    var dataRevision by remember { mutableIntStateOf(0) }

    val saveSizeBytes = remember(install.id, dataRevision) { store.dataBytes(install.id) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = {
                    Text(
                        install.name,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Rounded.ArrowBack, contentDescription = null)
                    }
                },
                actions = {
                    Box {
                        IconButton(onClick = { showMenu = true }) {
                            Icon(Icons.Rounded.MoreVert, contentDescription = null)
                        }
                        DropdownMenu(expanded = showMenu, onDismissRequest = { showMenu = false }) {
                            DropdownMenuItem(
                                text = { Text(stringResource(R.string.rename)) },
                                leadingIcon = { Icon(Icons.Rounded.Edit, contentDescription = null) },
                                onClick = {
                                    showMenu = false
                                    renameText = install.name
                                    showRename = true
                                }
                            )
                            DropdownMenuItem(
                                text = { Text(stringResource(R.string.remove_from_library)) },
                                leadingIcon = { Icon(Icons.Rounded.DeleteOutline, contentDescription = null) },
                                onClick = {
                                    showMenu = false
                                    showRemove = true
                                }
                            )
                        }
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(20.dp)
        ) {
            // Hero Card
            ElevatedCard(
                shape = RoundedCornerShape(24.dp),
                modifier = Modifier.fillMaxWidth()
            ) {
                Column(
                    modifier = Modifier.padding(20.dp),
                    verticalArrangement = Arrangement.spacedBy(16.dp)
                ) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(16.dp)
                    ) {
                        GameArtworkBadge(size = 64)
                        Column(
                            modifier = Modifier.weight(1f),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                install.name,
                                style = MaterialTheme.typography.titleLarge,
                                fontWeight = FontWeight.SemiBold,
                                maxLines = 2,
                                overflow = TextOverflow.Ellipsis
                            )
                            Text(
                                install.path,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }
                    }

                    FlowRow(
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                        verticalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        val (icon, tint, label) = when (status) {
                            is InstallStatus.Ready -> Triple(Icons.Rounded.CheckCircle, Color(0xFF2E9E5B), stringResource(R.string.ready))
                            is InstallStatus.Incomplete -> Triple(Icons.Rounded.WarningAmber, Color(0xFFE08E26), stringResource(R.string.incomplete, status.missing))
                            is InstallStatus.Missing -> Triple(Icons.Rounded.ErrorOutline, MaterialTheme.colorScheme.error, stringResource(R.string.missing))
                        }
                        StatusPill(icon = icon, tint = tint, text = label)

                        if (probe?.hasScPatch == true) {
                            StatusPill(
                                icon = Icons.Rounded.Translate,
                                tint = MaterialTheme.colorScheme.primary,
                                text = stringResource(R.string.simplified_chinese)
                            )
                        }
                    }

                    Button(
                        onClick = { store.play(install) },
                        enabled = store.canPlay(install.id),
                        shape = RoundedCornerShape(16.dp),
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp)
                    ) {
                        Icon(Icons.Rounded.PlayArrow, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(
                            stringResource(R.string.play),
                            style = MaterialTheme.typography.titleSmall
                        )
                    }
                }
            }

            // Engine Options
            M3SectionGroup(title = stringResource(R.string.engine_options)) {
                Column(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    Text(
                        stringResource(R.string.gpu_driver),
                        style = MaterialTheme.typography.bodyLarge
                    )
                    SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                        val drivers = GpuDriver.entries
                        drivers.forEachIndexed { idx, drv ->
                            SegmentedButton(
                                selected = install.options.driver == drv,
                                onClick = {
                                    store.update(install.id) {
                                        it.copy(options = it.options.copy(driver = drv))
                                    }
                                },
                                shape = SegmentedButtonDefaults.itemShape(index = idx, count = drivers.size)
                            ) {
                                Text(
                                    when (drv) {
                                        GpuDriver.AUTO -> stringResource(R.string.driver_auto)
                                        GpuDriver.VULKAN -> stringResource(R.string.driver_vulkan)
                                    }
                                )
                            }
                        }
                    }
                }
            }

            // Data & Saves
            M3SectionGroup(title = stringResource(R.string.data_saves)) {
                M3InfoListItem(
                    icon = Icons.Rounded.Storage,
                    label = stringResource(R.string.save_size),
                    value = Formatter.formatShortFileSize(context, saveSizeBytes)
                )
                if (install.lastPlayedMs > 0L) {
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                    val formatted = DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT)
                        .format(Date(install.lastPlayedMs))
                    M3InfoListItem(
                        icon = Icons.Rounded.History,
                        label = stringResource(R.string.last_played),
                        value = formatted
                    )
                }
                if (install.playSeconds > 0) {
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                    val mins = (install.playSeconds / 60).toInt()
                    val dur = if (mins < 60) "${mins}m" else "${mins / 60}h ${mins % 60}m"
                    M3InfoListItem(
                        icon = Icons.Rounded.Schedule,
                        label = stringResource(R.string.play_time),
                        value = dur
                    )
                }
                HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                Box(modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                    OutlinedButton(
                        onClick = { showClearSaves = true },
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                        shape = RoundedCornerShape(14.dp),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.clear_saves))
                    }
                }
            }

            // Diagnostics
            M3SectionGroup(title = stringResource(R.string.diagnostics)) {
                if (probe != null) {
                    M3InfoListItem(
                        icon = Icons.Rounded.Tag,
                        label = stringResource(R.string.game_id),
                        value = probe.gameID
                    )
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                    M3InfoListItem(
                        icon = Icons.Rounded.Archive,
                        label = stringResource(R.string.archives_found),
                        value = probe.arcsFound.toString()
                    )
                    if (probe.hasScPatch) {
                        HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                        M3InfoListItem(
                            icon = Icons.Rounded.Translate,
                            label = stringResource(R.string.script_patch),
                            value = stringResource(R.string.simplified_chinese)
                        )
                    }
                    if (probe.missing.isNotEmpty()) {
                        HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                        M3InfoListItem(
                            icon = Icons.Rounded.WarningAmber,
                            label = stringResource(R.string.issues),
                            value = probe.missing
                        )
                    }
                    HorizontalDivider(modifier = Modifier.padding(horizontal = 16.dp))
                }
                Box(modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                    FilledTonalButton(
                        onClick = { showLog = true },
                        shape = RoundedCornerShape(14.dp),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Icon(Icons.Rounded.Terminal, contentDescription = null)
                        Spacer(Modifier.width(8.dp))
                        Text(stringResource(R.string.view_log))
                    }
                }
            }

            // Library Management
            OutlinedButton(
                onClick = { showRemove = true },
                colors = ButtonDefaults.outlinedButtonColors(contentColor = MaterialTheme.colorScheme.error),
                shape = RoundedCornerShape(16.dp),
                modifier = Modifier
                    .fillMaxWidth()
                    .height(46.dp)
            ) {
                Icon(Icons.Rounded.DeleteOutline, contentDescription = null)
                Spacer(Modifier.width(8.dp))
                Text(stringResource(R.string.remove_from_library))
            }

            Spacer(Modifier.height(12.dp))
        }
    }

    if (showRename) {
        AlertDialog(
            onDismissRequest = { showRename = false },
            icon = { Icon(Icons.Rounded.Edit, contentDescription = null) },
            title = { Text(stringResource(R.string.rename_game)) },
            text = {
                OutlinedTextField(
                    value = renameText,
                    onValueChange = { renameText = it },
                    label = { Text(stringResource(R.string.game_name)) },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth()
                )
            },
            confirmButton = {
                Button(onClick = {
                    store.rename(install.id, renameText)
                    showRename = false
                }) { Text(stringResource(R.string.save)) }
            },
            dismissButton = {
                TextButton(onClick = { showRename = false }) { Text(stringResource(R.string.cancel)) }
            }
        )
    }

    if (showClearSaves) {
        AlertDialog(
            onDismissRequest = { showClearSaves = false },
            icon = { Icon(Icons.Rounded.DeleteOutline, contentDescription = null) },
            title = { Text(stringResource(R.string.clear_saves)) },
            text = { Text(stringResource(R.string.clear_saves_confirm, install.name)) },
            confirmButton = {
                TextButton(onClick = {
                    store.clearData(install.id)
                    dataRevision++
                    showClearSaves = false
                }) {
                    Text(stringResource(R.string.clear_saves), color = MaterialTheme.colorScheme.error)
                }
            },
            dismissButton = {
                TextButton(onClick = { showClearSaves = false }) { Text(stringResource(R.string.cancel)) }
            }
        )
    }

    if (showRemove) {
        AlertDialog(
            onDismissRequest = { showRemove = false },
            icon = { Icon(Icons.Rounded.DeleteOutline, contentDescription = null) },
            title = { Text(stringResource(R.string.remove_confirm_title, install.name)) },
            text = { Text(stringResource(R.string.remove_confirm_msg)) },
            confirmButton = {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    TextButton(onClick = {
                        showRemove = false
                        store.remove(install.id, deleteSaves = false)
                        onBack()
                    }) { Text(stringResource(R.string.remove_keep_saves)) }
                    TextButton(onClick = {
                        showRemove = false
                        store.remove(install.id, deleteSaves = true)
                        onBack()
                    }) { Text(stringResource(R.string.remove_delete_saves), color = MaterialTheme.colorScheme.error) }
                }
            },
            dismissButton = {
                TextButton(onClick = { showRemove = false }) { Text(stringResource(R.string.cancel)) }
            }
        )
    }

    if (showLog) {
        val clipboard = LocalClipboardManager.current
        var copied by remember { mutableStateOf(false) }
        val text = remember(install.id, showLog) { store.logText(install.id) }
        AlertDialog(
            onDismissRequest = { showLog = false },
            icon = { Icon(Icons.Rounded.Terminal, contentDescription = null) },
            title = { Text(stringResource(R.string.last_run_log)) },
            text = {
                Surface(
                    shape = RoundedCornerShape(12.dp),
                    color = MaterialTheme.colorScheme.surfaceContainerLow,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(280.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .padding(12.dp)
                    ) {
                        Text(
                            text = text.ifEmpty { stringResource(R.string.no_log) },
                            fontFamily = FontFamily.Monospace,
                            style = MaterialTheme.typography.bodySmall
                        )
                    }
                }
            },
            confirmButton = {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    if (text.isNotEmpty()) {
                        TextButton(onClick = {
                            clipboard.setText(AnnotatedString(text))
                            copied = true
                        }) { Text(stringResource(if (copied) R.string.copied else R.string.copy)) }
                        TextButton(onClick = {
                            store.clearLog(install.id)
                            showLog = false
                        }) { Text(stringResource(R.string.clear), color = MaterialTheme.colorScheme.error) }
                    }
                    TextButton(onClick = { showLog = false }) { Text(stringResource(R.string.ok)) }
                }
            }
        )
    }
}

@Composable
private fun StatusPill(icon: ImageVector, tint: Color, text: String) {
    Surface(
        shape = RoundedCornerShape(10.dp),
        color = tint.copy(alpha = 0.12f)
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            Icon(icon, contentDescription = null, tint = tint, modifier = Modifier.size(16.dp))
            Text(
                text,
                style = MaterialTheme.typography.labelMedium,
                color = tint
            )
        }
    }
}

@Composable
private fun M3SectionGroup(title: String, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(
            title,
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.primary,
            modifier = Modifier.padding(horizontal = 4.dp)
        )
        OutlinedCard(
            shape = RoundedCornerShape(20.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            content()
        }
    }
}

@Composable
private fun M3InfoListItem(icon: ImageVector, label: String, value: String) {
    ListItem(
        headlineContent = { Text(label, style = MaterialTheme.typography.bodyMedium) },
        leadingContent = {
            Icon(
                icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant
            )
        },
        trailingContent = {
            Text(
                value,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = FontWeight.Medium,
                color = MaterialTheme.colorScheme.onSurface
            )
        },
        colors = ListItemDefaults.colors(containerColor = Color.Transparent)
    )
}
