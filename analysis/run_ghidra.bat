@echo off
rem Imports + analyzes the game exe, then exports decompiled C / strings / function index to analysis\out
set GH=D:\CodeSpace\ghidra_12.2_PUBLIC
set HERE=%~dp0
set GAME=E:\Games\Amairo Chocolate 2
call "%GH%\support\analyzeHeadless.bat" "%HERE%ghidra_project" OpenBGI -import "%GAME%\ac2_ML.exe" -overwrite -scriptPath "%HERE%scripts" -postScript ExportAll.java "%HERE%out" -log "%HERE%out\ghidra.log"
