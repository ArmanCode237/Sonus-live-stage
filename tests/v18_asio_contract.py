# main.cpp was split into src/app/** (StageApp + route groups): contract checks
# read the whole application layer instead of the single former file.
def _stage_app_source():
    import pathlib as _p
    root = _p.Path(__file__).resolve().parents[1]
    files = [root / 'src/main.cpp', *sorted((root / 'src/app').rglob('*.h')), *sorted((root / 'src/app').rglob('*.cpp'))]
    return chr(10).join(f.read_text(encoding='utf-8') for f in files)
"""Static regression: ASIO build defaults and user-facing failure diagnoses."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cmake=(root/'CMakeLists.txt').read_text()
main=_stage_app_source()
settings=(root/'ui/js/settings.js').read_text()
html=(root/'ui/settings.html').read_text()
assert 'option(STAGE_ENABLE_ASIO "Build native ASIO output with RtAudio on Windows" ON)' in cmake
assert 'set(RTAUDIO_API_ASIO ${STAGE_ENABLE_ASIO} CACHE BOOL "" FORCE)' in cmake
assert '-DSTAGE_ENABLE_ASIO=ON' in (root/'scripts/build_windows.ps1').read_text()
assert '-DSTAGE_ENABLE_ASIO=ON' in (root/'scripts/build_windows_mingw.ps1').read_text()
assert 'LiveStageAudioProbe' in cmake
for item in ('asioCompiled','asioDeviceCount','audioProbeErrors','lastAudioError', 'dac_->getErrorText()', 'pushBuffer(256)', 'pushBuffer(512)', 'tryOpenInternal(prevUid, prevSr, prevBuf'):
    assert item in main,item
for item in ('renderAsioDiagnostic', 'ASIO — NO COMPILADO', 'hw.ok', 'applyAudioButton'):
    assert item in settings,item
assert 'id="asioDiagnostic"' in html
print('V18 ASIO static contract OK')
