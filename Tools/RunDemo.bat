@echo off
rem Launches ReindeerHerd standalone at 1080p with the stat overlay for recording the demo video.
rem Usage: double-click, or "RunDemo.bat 1" to start in CPU mode (herd.Mode 1).
set MODE=%1
if "%MODE%"=="" set MODE=0
start "" "D:\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\ReindeerHerd.uproject" /Game/HerdLevel -game -windowed -ResX=1920 -ResY=1080 -nosplash -ExecCmds="r.SetRes 1920x1080w,herd.Mode %MODE%,r.VSync 0,stat fps,stat unit"
