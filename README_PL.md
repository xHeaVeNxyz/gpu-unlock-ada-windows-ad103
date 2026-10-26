# Eksperymentalny GPU Unlock Ada — Windows / AD103

**Stan projektu:** Stage 62, eksperyment na jednym komputerze z 26 października 2026 r. Repozytorium jest prywatne i początkowo zawiera dokumentację. Źródła Stage 62 zostały przygotowane w osobnej paczce do publikacji.

## Sprzęt

- Gigabyte GeForce RTX 4070 Ti SUPER Windforce OC, 16 GB (AD103, PCI `10DE:2705`)
- MSI PRO B650S WIFI
- Windows 11 x64, sterownik NVIDIA 621.01, CUDA 13.4
- VBIOS AORUS wgrany na kartę Gigabyte — **nie jest to test fabrycznego VBIOS-u Windforce**

## Wynik

Log Stage 62 wykazał `Transaction result=0` i `TRANSACTION OK`. Następnie karta była wykrywana w Windows.

| Operacja | Najlepszy odnotowany wynik |
|---|---:|
| FP16 → FP16 | 190,6 TFLOPS |
| FP16 → FP32 | 190,5 TFLOPS |
| BF16 → FP32 | 189,9 TFLOPS |
| INT8 → INT32 | 379,9 TOPS |
| FP32 FFMA | 45,6 TFLOPS |

**To nie jest udokumentowany wzrost wydajności 2× na tej konkretnej karcie.** Brakuje kontrolowanego pomiaru sprzed modyfikacji na identycznym sprzęcie. Test MMA nie mierzy FPS ani rzeczywistej wydajności aplikacji.

## Ostrzeżenie

Aktywne eksperymenty UEFI z FWSEC, DMA, FLR i przekazaniem GPU sterownikowi mogą uniemożliwić uruchomienie Windows lub odzyskanie obrazu. Nie ma gwarantowanego automatycznego cofnięcia zmian. Nie publikujemy plików EFI, firmware NVIDIA, ROM-ów, numerów UUID ani prywatnych danych z logów.

Opis po angielsku: [README](README.md). Dane pomiarowe: [RESULTS](docs/RESULTS.md).
