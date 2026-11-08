@echo off
rem Builds the game's code library for the MH4U Android app from your own MH4U v1.3.0 update (see build-android-library.ps1)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-android-library.ps1" %*
