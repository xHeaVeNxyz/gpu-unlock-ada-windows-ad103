# GPU Unlock Ada — eksperymentalny port Windows / AD103 (Stage 62)

**Stan na 26.10.2026 • badania na jednym komputerze • brak stabilnej wersji**

Repozytorium zawiera **komplet opublikowanych źródeł Stage 62**, skrypty do kompilacji i weryfikacji offline, dokumentację testów oraz licencje. Dotyczy eksperymentów z **Gigabyte RTX 4070 Ti SUPER Windforce OC 16 GB** (AD103, `10DE:2705`) pod Windows 11 x64.

> [!CAUTION]
> Kod wykonuje eksperymentalne operacje UEFI na sprzęcie. Uruchomienie skompilowanego EFI grozi utratą obrazu lub problemami z rozruchem. Konfiguracja Stage 62 **nie zapewnia sprawdzonego rollbacku ani awaryjnego przełączenia na AMD iGPU**. Nie jest to gotowe, uniwersalne narzędzie.

## Przetestowana konfiguracja

| Element | Dane |
|---|---|
| GPU | Gigabyte RTX 4070 Ti SUPER Windforce OC, 16 GB |
| Identyfikator | PCI `10DE:2705`, subsystem `1458:4136` |
| VBIOS | AORUS po cross-flashu (nie fabryczny Windforce) |
| Płyta główna | MSI PRO B650S WIFI |
| System | Windows 11 x64 |
| Sterownik / CUDA | NVIDIA 621.01 / CUDA 13.4 |
| Etap eksperymentu | Stage 62 — sprawdzanie mapowania DMA z pustym tokenem |

W logu próby odnotowano `Transaction result=0` oraz `TRANSACTION OK`. Windows następnie się uruchomił. To **jedna obserwacja na określonym sprzęcie**, a nie gwarancja zgodności innych kart.

## Benchmark CUDA

| Operacja | Zgłoszony najlepszy wynik |
|---|---:|
| FP16 → FP16 | 190,6 TFLOPS |
| FP16 → FP32 | 190,5 TFLOPS |
| BF16 → FP32 | 189,9 TFLOPS |
| INT8 → INT32 | 379,9 TOPS |
| FP32 FFMA | 45,6 TFLOPS |

Są to wyniki **syntetycznego benchmarku instrukcji**, nie FPS w grach. Wynik FFMA jest zachowany w oryginalnym lokalnym transkrypcie, a szczytowe wartości Tensor pochodzą z podsumowania testów. **Nie mamy pomiaru sprzed odblokowania na tej samej karcie w identycznych warunkach**, więc nie można stwierdzić, że w tym przypadku osiągnięto wzrost o 2×.

## Dokumentacja

- [Pełne wyniki i ograniczenia](docs/RESULTS.md)
- [Instrukcje kompilacji i sprawdzania kodu offline](docs/BUILD.md)
- [Architektura i najważniejsze pliki](docs/ARCHITECTURE.md)
- [Zagrożenia i zasady publikacji](docs/SAFETY.md)
- [Zestawienie benchmarków CSV](data/benchmark-summary.csv)
- [Historia projektu](CHANGELOG.md)
- [English README](README.md)

Źródła znajdują się w katalogu [`src/`](src/), a skrypty w [`tools/`](tools/). Kompilator tworzy **aktywny, ryzykowny plik EFI** — sama kompilacja nie stanowi testu bezpieczeństwa. Nie publikujemy prywatnego `src/local/boot_profile.h`, obrazów firmware, plików ROM, wynikowych EFI ani nieoczyszczonych logów z danymi systemowymi.

Projekt opiera się na wcześniejszych pracach GPL-2.0-only. Zachowano [licencje](LICENSE), [informacje o autorach](THIRD_PARTY_NOTICES.md) i [dodatkowe licencje](LICENSES/). Nie jest powiązany z NVIDIA ani przez nią wspierany.
