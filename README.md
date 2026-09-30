# GMR Synth Quick Test

Teste mínimo do Akai MPK Mini Plus com JUCE no Linux. Ao pressionar uma tecla,
o aplicativo produz uma senoide polifônica e mostra a nota e a velocity recebidas.

## Compilar

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

O primeiro configure baixa o JUCE 9.0.3 pelo CMake FetchContent.

## Executar com PipeWire/JACK

```bash
PIPEWIRE_QUANTUM=64/48000 pw-jack ./build/GmrSynthQuickTest_artefacts/Release/gmr-synth-quick-test
```

Se o hardware apresentar cortes ou xruns, use primeiro 128 amostras:

```bash
PIPEWIRE_QUANTUM=128/48000 pw-jack ./build/GmrSynthQuickTest_artefacts/Release/gmr-synth-quick-test
```

Para baixa latência real, use fones ou uma interface de áudio cabeada, não a
saída Bluetooth.
