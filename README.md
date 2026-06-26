# TrueAudioRand

Libreria C che genera numeri veramente casuali (TRNG) sfruttando l'entropia del rumore di fondo del microfono predefinito del sistema.

## Architettura

- **Backend audio:** [PortAudio](http://www.portaudio.com/) (cross-platform)
- **Estrazione entropia:** differenze tra campioni consecutivi (componente AC del rumore)
- **Debiasing:** algoritmo di Von Neumann sui bit grezzi
- **Sbiancamento:** SHA-256 come randomness extractor (RK)

## API

```c
int tar_init(void);           /* apre microfono: 44100 Hz, mono, PCM 16-bit */
uint32_t tar_get_rand32(void); /* restituisce un uint32 sbiancato */
void tar_close(void);          /* chiude stream e PortAudio */
const char *tar_strerror(void);
```

## Compilazione

### Linux (Debian/Ubuntu)

```bash
sudo apt install build-essential libportaudio2 libportaudio-dev
make
```

### macOS

```bash
brew install portaudio
make
```

### Windows (MSYS2 MinGW64)

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-portaudio
make
```

## Utilizzo

### Applicazione GUI (consigliato)

```powershell
.\run_gui.bat
```

Oppure avvia direttamente `TrueAudioRand.exe`. Dalla finestra puoi:
- **Scegliere il microfono** dal menu a tendina
- **Demo pipeline** — mostra ogni fase del processo nel log
- **Genera campioni** — salva `random_samples.txt`
- **Analizza grafici** — esegue `verify_rand.py` e crea i PNG
- **Istogramma / Scatter** — apre i grafici generati

### Processo completo da terminale

```powershell
.\run_process.bat              # compila + demo + 100k campioni + grafici
.\run_process.bat -Quick       # versione rapida (1000 campioni)
.\run_process.bat -DemoOnly    # solo spiegazione del pipeline
```

> Se usi direttamente `run_process.ps1`, PowerShell potrebbe bloccarlo per policy di sicurezza.
> Usa `run_process.bat` oppure: `powershell -ExecutionPolicy Bypass -File .\run_process.ps1`

### Comandi singoli

```bash
# Mostra come funziona un ciclo (microfono -> SHA-256 -> uint32)
./main_test --demo

# Genera campioni (microfono attivo)
./main_test                    # 100.000 campioni
./main_test --count 5000       # numero personalizzato

# Analisi Python
pip install -r requirements.txt
python verify_rand.py random_samples.txt
```

Output grafici: `histogram.png`, `scatter.png`.

## Interpretazione grafici

- **Istogramma:** la distribuzione osservata dovrebbe essere piatta (uniforme). La curva gaussiana rossa mostra come *non* dovrebbe apparire un TRNG ben sbiancato.
- **Scatter N vs N+1:** assenza di pattern visibili indica bassa correlazione tra campioni consecutivi.

## Note

- Lascia il microfono attivo e non muto durante la generazione.
- Ambienti silenziosi possono rallentare l'accumulo di entropia; un lieve rumore di fondo è benefico.
- Questa libreria è adatta a scopi educativi e sperimentali, non a crittografia ad alto livello senza ulteriori test statistici (es. NIST SP 800-22).
