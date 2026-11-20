@echo off
setlocal
cd /d "%~dp0webui"
if not exist "node_modules\vite" (
  echo [Stage] Installing React dependencies...
  call npm ci || exit /b 1
)
echo [Stage] Building the current React UI, including state transaction fixes...
call npm run build || exit /b 1
echo [Stage] The updated frontend is now available under ui\app.
exit /b 0
