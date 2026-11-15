cmake -B build
cmake --build build
.\build\NordVirtualStage.exe ..\LIVE_STAGE_SAMPLES\samples

.\build\NordVirtualStage.exe .\samples






scripts por si hay fechas futuras:

comprueba si hay fechas futuras: 
powershell -NoProfile -Command "Get-ChildItem -Recurse -File | Where-Object { $_.LastWriteTime -gt (Get-Date) } | Select-Object FullName, LastWriteTime"


corrige las fechas:
powershell -NoProfile -Command "$now=Get-Date; Get-ChildItem -Recurse -File -Force | Where-Object { $_.LastWriteTime -gt $now } | ForEach-Object { $_.LastWriteTime=$now }"