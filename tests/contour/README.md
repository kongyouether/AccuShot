# Contour evaluation

The production C++ algorithm is linked by `contour_current`. `baseline.cpp` is a frozen copy of the original implementation, retained for reproducibility. The probe uses only image pixels, not UIA, DOM, filenames, or annotations to generate candidates. Annotations select query points and score results after inference.

From the repository root in the MSVC x64 development environment, with Qt bin on PATH:

```powershell
cmake -S tests/contour -B build-contour -G Ninja -DCMAKE_BUILD_TYPE=Release -DQt6_DIR=C:/Qt/6.11.1/msvc2022_64/lib/cmake/Qt6
cmake --build build-contour --parallel 6
$env:QT_QPA_PLATFORM='offscreen'
./build-contour/contour_regression.exe
./build-contour/contour_baseline.exe docs/evidence/contour-2026-10-18/manifest.json docs/evidence/contour-2026-10-18/baseline.json
./build-contour/contour_current.exe docs/evidence/contour-2026-10-18/manifest.json docs/evidence/contour-2026-10-18/current.json
python tests/contour/report.py
```

`report.py` needs Pillow and NumPy. The optional Python CV prototype also needs OpenCV. Model experiments are isolated in `build-contour/model-env`; exact installed versions are in the evidence directory. No model is a product dependency:

```powershell
./build-contour/model-env/Scripts/python.exe tests/contour/experiment.py model
./build-contour/model-env/Scripts/python.exe tests/contour/experiment.py model doclayout_docstructbench
```

These commands download models if absent; all screenshot inference is local. Provider, startup timing, raw model detections, per-point candidate lists, preprocessing timing samples and plots are saved separately. The original five development images were used for diagnosis/tuning. Two `holdout` images were captured and labelled after the C++ algorithm was frozen; neither their scores nor plots were used to tune it. They are within the same websites, not an independent broad-domain benchmark.

Native integration: build `tests/result-ui`, then run `result_ui.exe contourAsyncSelection` with `QT_QPA_PLATFORM=windows`. It temporarily moves/restores the cursor and creates a synthetic fixture. It tests real Windows screen capture and overlay with synthetic Qt input; it does not constitute manual testing of all third-party windows.
