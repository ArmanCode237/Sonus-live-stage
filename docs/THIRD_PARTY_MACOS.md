# Avisos de terceros — compilación para macOS

Este proyecto incorpora o descarga bibliotecas que conservan sus propias licencias.
La licencia principal del proyecto está en `LICENSE` (AGPL-3.0 según el paquete adjunto).

- **RtAudio 6.0.1**: API de audio multiplataforma; licencia del repositorio upstream.
- **RtMidi 6.0.0**: API MIDI multiplataforma; licencia del repositorio upstream.
- **JUCE 8.0.8**: marco de plugin VST3/AU y editor nativo. Verificar los términos GPL/comerciales según la distribución.
- **webview/webview 0.12.0**: ventana WKWebView/Cocoa; licencia de su repositorio.
- **nlohmann/json v3.11.3**: serialización; licencia de su repositorio.
- **Munt / libmt32emu**: LGPL-2.1+ según `third_party/mt32emu/COPYING.LESSER.txt`. **Se enlaza estáticamente** en esta versión. Antes de distribuir, cumplir las obligaciones de la LGPL sobre sustitución/re-enlazado o adoptar una estrategia compatible.
- **Motor DX7 msfa**: Apache-2.0 (véase `third_party/msfa/NOTICE.txt`).

No se deben incluir ROMs de Roland MT-32 ni librerías de muestras de terceros sin autorización.
Este documento no sustituye una auditoría legal ni las licencias originales.
