package dev.net64.paperboat

object SettingsCatalog {
    data class Row(val title: String, val help: String, val key: String = "", val values: List<Float> = emptyList(),
        val labels: List<String> = emptyList(), val default: Float = 0f, val floating: Boolean = false, val action: String = "",
        val restart: Boolean = false)
    private fun toggle(title: String, key: String, default: Float = 0f, help: String = "Toggle $title.") =
        Row(title, help, key, listOf(0f,1f), listOf("Off","On"), default)
    // Engine.cpp reads these: archives at start-up, post passes every frame.
    private const val FEATURE = "gEnhancements.Features."
    fun rows(page:String): List<Row> = when(page) {
        "Cheats" -> listOf(
            toggle("Infinite HP", "gCheats.InfiniteHealth",help="Mario's HP won't decrease during battle."),
            toggle("Infinite FP", "gCheats.InfiniteFlowerPoints",help="Mario's FP won't decrease during battle."),
            toggle("No Badge Cost", "gCheats.NoBPCost",help="Equip badges regardless of BP cost."),
            toggle("Max Star Power", "gCheats.MaxStarPower",help="Star Power won't decrease."),
            toggle("Power Bounce Chance", "gCheats.MaxPowerBounceChance",help="Keep bounce continuation chance high; timing still matters."),
            toggle("2x Star Points & Coins", "gCheats.DoubleRewards",help="Enemies drop twice the Star Points and coins."))
        "Graphics" -> listOf(
            Row("Internal Resolution", "Scale the rendering resolution.", "gSettings.InternalResolution", listOf(.5f,.75f,1f,1.25f,1.5f,2f),listOf("50%","75%","100%","125%","150%","200%"),1f,true),
            Row("Frame Rate", "Frame interpolation target; actual performance varies.", "gSettings.InterpolationFPS",listOf(30f,60f,90f,120f),listOf("30 FPS","60 FPS","90 FPS","120 FPS"),30f),
            toggle("Match Refresh Rate", "gSettings.MatchRefreshRate"),
            Row("Anti-Aliasing (MSAA)","Multisample anti-aliasing.","gSettings.MSAAValue",listOf(1f,2f,4f,8f),listOf("Off","2x","4x","8x"),1f),
            Row("2D Texture Filter","Filter 2D sprites.","gSettings.2DTextureFilter",listOf(0f,1f),listOf("Nearest","Linear")),
            Row("Dialogue Text Filter","Filter dialogue text.","gSettings.DialogueTextFilter",listOf(0f,1f),listOf("Nearest","Linear")),
            Row("3D Texture Filter","Filtering for 3D textures. Applies after Restart Game.","gTextureFilter",listOf(0f,1f,2f),listOf("Three-Point","Linear","None"),restart=true),
            toggle("Story Book", FEATURE+"StoryBook",help="Painted storybook look: ink outlines, canvas grain and colour grade."),
            toggle("CRT Filter", FEATURE+"CRT",help="CRT screen with scanlines and phosphor mask. By @Lywx."),
            toggle("Full Height View","gEnhancements.Graphics.FullHeightView",help="Remove the letterbox bars at the top and bottom in gameplay."),
            toggle("Rounded Projector Reel","gEnhancements.Graphics.RoundedReel",help="Rounded battle projector reel for widescreen."),
            toggle("Disable Mipmaps","gEnhancements.Graphics.DisableMipmaps",help="Sharper distant textures, like an emulator."),
            Row("VSync","Sync frames to the display. Applies after Restart Game.","gVsyncEnabled",listOf(0f,1f),listOf("Off","On"),1f,restart=true),
            Row("Status Bar","Layout, size and contents of the HP/FP/coins bar.",action="page:StatusBar"),
            Row("Advanced Graphics", "Open all original engine settings.", action="advanced"))
        // Subpage of Graphics: the PC Enhancements > Status Bar page. Colour
        // pickers stay in the Original Engine Menu.
        "StatusBar" -> listOf(
            toggle("Always Show Status Bar","gEnhancements.StatusBar.AlwaysShow",help="Keep HP/FP/coins on screen in the overworld."),
            Row("Status Bar Scale","Size of the whole status bar.","gEnhancements.StatusBar.Scale",(2..8).map{it*25f},(2..8).map{"${it*25}%"},100f),
            toggle("Show HP","gEnhancements.StatusBar.ShowHP",1f,"Show Mario's HP."),
            toggle("Show FP","gEnhancements.StatusBar.ShowFP",1f,"Show Flower Points."),
            toggle("Show Star Power","gEnhancements.StatusBar.ShowStarPower",1f,"Show the Star Power gauge."),
            toggle("Show Star Points","gEnhancements.StatusBar.ShowStarPoints",1f,"Show the Star Point counter."),
            toggle("Show Coins","gEnhancements.StatusBar.ShowCoins",1f,"Show the coin counter."),
            toggle("Show Star Pieces","gEnhancements.StatusBar.ShowStarPieces",help="Add the Star Piece counter."),
            toggle("Show Badges","gEnhancements.StatusBar.ShowBadges",help="Add the badge counter."),
            toggle("Background","gEnhancements.StatusBar.ShowBackground",1f,"Draw the box behind the status bar."),
            toggle("Flat Bottom","gEnhancements.StatusBar.FlatBottom",help="One box height all the way across."),
            toggle("Flat Color","gEnhancements.StatusBar.FlatColor",help="One solid box colour, no highlight or shadow."),
            Row("Box Width","How far the box reaches across the screen.","gEnhancements.StatusBar.BoxWidth",(1..5).map{it*20f},(1..5).map{"${it*20}%"},100f),
            Row("Box Height","How tall the box is.","gEnhancements.StatusBar.BoxHeight",(2..8).map{it*25f},(2..8).map{"${it*25}%"},100f),
            Row("Reset Status Bar","Put every option on this page back to its default.",action="reset"))
        "Audio" -> listOf("Master" to "Master", "Music" to "MainMusic", "Environment" to "Environment", "Sound Effects" to "SFX").map {
            Row(it.first+" Volume", "Adjust ${it.first.lowercase()} volume.", "gSettings.Volume.${it.second}", (0..10).map { n->n*10f },(0..10).map { n->"${n*10}%" },100f)
        }
        "Controls" -> listOf(
            toggle("Touch Controls", "gTouchControls.Enabled",1f,"Show or hide the on-screen controls. Menu stays available."),
            Row("Layout Editor", "Drag buttons and stick. Done saves; Reset restores defaults.", action="layout"),
            Row("Button Mapping", "Configure physical controller bindings.",action="bindings"),
            Row("Touch Control Scale", "Change the size of on-screen controls.","gTouchControls.Scale",listOf(.5f,.75f,1f,1.25f,1.5f,2f),listOf("50%","75%","100%","125%","150%","200%"),1f,true),
            Row("Touch Opacity", "Change the opacity of on-screen controls.","gTouchControls.Opacity",listOf(.2f,.4f,.6f,.8f,1f),listOf("20%","40%","60%","80%","100%"),.8f,true),
            toggle("D-Pad as Left Stick", "gSettings.Controls.DPadAsLeftStick"),
            toggle("Menu Controller Navigation", "gSettings.ControlNav",1f))
        "System" -> listOf(
            Row("HD Textures","HD interface, icons and backgrounds. Applies after Restart Game.",FEATURE+"HDTextures",listOf(0f,1f),listOf("Off","On"),1f,restart=true),
            Row("Game Language","Game text language. Applies after Restart Game.",FEATURE+"Language",listOf(0f,1f,2f),listOf("English","Español","Français"),restart=true),
            Row("Clean Text","Sharp 4K dialogue font. Applies after Restart Game.",FEATURE+"CleanText",listOf(0f,1f),listOf("Off","On"),1f,restart=true),
            Row("Button Prompts","On-screen button icons. Applies after Restart Game.",FEATURE+"ButtonPrompts",listOf(0f,1f,2f),listOf("N64","Xbox","Switch"),1f,restart=true),
            Row("Restart Game","Restart now to apply language, text and button changes.",action="restart"),
            Row("Saved Games","Import, export and manage saved games.",action="saves"),Row("Mods","Import, enable or remove mods.",action="mods"),
            toggle("Sprint Button", "gEnhancements.SprintButton"), toggle("Free QoL Badges","gEnhancements.FreeQoLBadges"),
            toggle("MQ Jr. Enemy Stats",FEATURE+"MQJrBalance",help="Master Quest Jr. enemy HP, levels and rewards. Next battle."),
            // Gameplay, from the PC Enhancements > Gameplay page.
            Row("Action Commands","Widen attack action command windows.","gEnhancements.ActionCommandDifficulty",listOf(0f,1f,2f,3f),listOf("Original","Easy","Easier","Easiest")),
            Row("Block Window","Timed block window. Wider overrides Dodge Master.","gEnhancements.BlockWindowMode",listOf(0f,1f,2f),listOf("3 frames","7 frames","10 frames")),
            Row("Show Hidden Blocks","When hidden blocks are visible.","gEnhancements.ShowHiddenBlocks",listOf(0f,1f,2f,3f),listOf("Original","Watt in party","Watt joined","Always")),
            toggle("Always Chuck Quizmo","gEnhancements.AlwaysChuckQuizmo",help="Chuck Quizmo appears in the current map."),
            toggle("Mute I Spy Sound","gEnhancements.MuteISpySound",help="Silence the I Spy notification sound."),
            toggle("Prevent Loading Zone Storage","gEnhancements.PreventLoadingZoneStorage",help="Patch the loading zone storage glitch. Can freeze Mario midair."),
            Row("Original Engine Menu","Access all settings, enhancements, shaders and developer tools.",action="advanced"))
        // Read by src/port/android/OpenXR.cpp. Only a headset with an OpenXR
        // runtime uses them; on a phone they do nothing.
        "VR Settings" -> listOf(
            Row("OpenXR","Use the headset when one is present. Applies after Restart Game.","gXR.Enabled",listOf(0f,1f),listOf("Off","On"),1f,restart=true),
            Row("View Mode","Flat screen, 3D diorama, or (experimental) through Mario's eyes.","gXR.Immersive",listOf(0f,1f,2f),listOf("Virtual Screen","Immersive 3D","First Person"),1f),
            Row("First Person Size","First Person: how big the world is around you.","gXR.FirstPersonScale",listOf(40f,32f,25f,20f,15f),listOf("60%","80%","100%","125%","165%"),25f,true),
            Row("World Size","Immersive: how big the world looks around you.","gXR.WorldScale",listOf(450f,300f,200f,150f,100f,75f),listOf("33%","50%","75%","100%","150%","200%"),150f,true),
            toggle("Hand Hammer","gXR.Hammer",1f,"First Person: hold a hammer in your right hand; swing it to hammer."),
            toggle("Smooth Turning","gXR.SmoothTurn",help="Turn your view with the right stick. Left stick then moves where you face."),
            Row("Turn Speed","Smooth Turning: how fast the view turns.","gXR.TurnSpeed",listOf(45f,90f,135f,180f),listOf("Slow","Normal","Fast","Very Fast"),90f,true),
            Row("Draw Distance","Immersive: draw scenery beyond the game's own view, so less pops in.","gXR.DrawDistance",listOf(0f,1f,2f),listOf("Normal","Far","Maximum")),
            toggle("Level Horizon","gXR.LevelHorizon",1f,"Immersive: stand the world upright instead of tilting with the camera."),
            Row("HUD Distance","Immersive: how far away the HUD and text float.","gXR.HudDistance",listOf(1f,1.4f,1.8f,2.5f,3.5f),listOf("1.0 m","1.4 m","1.8 m","2.5 m","3.5 m"),1.8f,true),
            Row("Backdrop Distance","Immersive: how far away background pictures sit.","gXR.BackdropDistance",listOf(10f,20f,40f,80f),listOf("10 m","20 m","40 m","80 m"),40f,true),
            Row("Screen Distance","Virtual screen: how far away it is.","gXR.ScreenDistance",listOf(1.5f,2f,2.5f,3.5f,5f),listOf("1.5 m","2.0 m","2.5 m","3.5 m","5.0 m"),2.5f,true),
            Row("Screen Size","Virtual screen: how wide it is.","gXR.ScreenWidth",listOf(2f,2.8f,3.6f,5f,7f),listOf("2.0 m","2.8 m","3.6 m","5.0 m","7.0 m"),3.6f,true),
            Row("Screen Height","Virtual screen: raise or lower it from eye level.","gXR.ScreenHeight",listOf(-.6f,-.3f,0f,.3f,.6f),listOf("-0.6 m","-0.3 m","Eye level","+0.3 m","+0.6 m"),0f,true))
        else -> emptyList()
    }

}
