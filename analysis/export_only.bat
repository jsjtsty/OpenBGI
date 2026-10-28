@echo off
rem Re-run only the export script on the already-analyzed project
set GH=D:\CodeSpace\ghidra_12.2_PUBLIC
set HERE=%~dp0
call "%GH%\support\analyzeHeadless.bat" "%HERE%ghidra_project" OpenBGI -process ac2_ML.exe -noanalysis -scriptPath "%HERE%scripts" -postScript ExportAll.java "%HERE%out" -log "%HERE%out\ghidra_export.log"
