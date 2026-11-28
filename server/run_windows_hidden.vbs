' ============================================================================
' VortexRemoteBrowser Native Windows Invisible Background Launcher (Zero CMD Window)
' Double-click to run VortexRemoteBrowser Server silently in the system tray.
' Pass "--no-tray" to run 100% invisibly without even a system tray icon.
' Use stop_windows.bat (or tray menu -> Stop & Exit) to stop cleanly.
' ============================================================================
Option Explicit

Dim fso, shell, scriptDir, venvPyw, trayScript, installPs1, args, i, procEnv, sysPath, usrPath

Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")

scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
If Right(scriptDir, 1) <> "\" Then scriptDir = scriptDir & "\"

venvPyw    = scriptDir & ".venv\Scripts\pythonw.exe"
trayScript = scriptDir & "run_windows_tray.pyw"
installPs1 = scriptDir & "install_windows.ps1"

' Refresh PATH from registry so winget-installed ffmpeg / Python are visible
On Error Resume Next
sysPath = shell.RegRead("HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment\Path")
usrPath = shell.RegRead("HKCU\Environment\Path")
Set procEnv = shell.Environment("Process")
If Len(sysPath) > 0 Or Len(usrPath) > 0 Then
    procEnv("PATH") = sysPath & ";" & usrPath & ";" & procEnv("PATH")
End If
On Error GoTo 0

' First-time setup: run visible PowerShell installer if .venv is missing
If Not fso.FileExists(venvPyw) Then
    shell.Run "powershell.exe -NoProfile -ExecutionPolicy Bypass -File """ & installPs1 & """", 1, True
    If Not fso.FileExists(venvPyw) Then
        WScript.Quit 1
    End If
End If

args = ""
For i = 0 To WScript.Arguments.Count - 1
    args = args & " """ & Replace(WScript.Arguments(i), """", """""") & """"
Next

shell.CurrentDirectory = scriptDir
' Window style 0 (vbHide) + pythonw.exe = zero shell window flash
shell.Run """" & venvPyw & """ """ & trayScript & """" & args, 0, False
