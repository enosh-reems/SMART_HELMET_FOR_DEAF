import sys
import random
from pathlib import Path

import torch
import torchaudio
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader

sys.path.insert(0, str(Path(__file__).resolve().parent))
from light_siren_cnn import LightSirenCNN

# ============================================================
# PATHS
# ============================================================
PROJECT = Path(__file__).resolve().parents[2]
POSITIVE_DIR = PROJECT / "datasets" / "AudioSet_EV_v2PANNs_2020" / "Positive_files"
NEGATIVE_DIR = PROJECT / "datasets" / "AudioSet_EV_v2PANNs_2020" / "Negative_files"
REAL_WORLD_DIR = PROJECT / "real_world_siren_train" / "windows"
REAL_WORLD_NEG_DIR = PROJECT / "real_world_hard_negatives" / "windows"

OUTPUT_DIR = PROJECT / "checkpoints" / "light_siren_realworld_v2"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

BEST_PATH = OUTPUT_DIR / "best.pt"

# ============================================================
# SETTINGS
# ============================================================
SEED = 42
SAMPLE_RATE = 16000
DURATION = 10
TARGET_SAMPLES = SAMPLE_RATE * DURATION
N_MELS = 64
IMAGE_SIZE = 128
BATCH_SIZE = 32
EPOCHS = 10
LEARNING_RATE = 1e-3

random.seed(SEED)
torch.manual_seed(SEED)

device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

# ============================================================
# AUDIO FILE DISCOVERY
# ============================================================
BAD_FILES = {
    "eqnveHA0hrI.wav",
    "mW3S0u8bj58.wav",
}

def find_audio(folder):
    return sorted(
        [
            p for p in folder.rglob("*")
            if p.suffix.lower() == ".wav"
            and p.name not in BAD_FILES
        ]
    )

def get_audioset_positive():
    csv_path = PROJECT / "datasets" / "AudioSet_EV_v2PANNs_2020" / "EV_Positives.csv"

    import pandas as pd

    df = pd.read_csv(csv_path)
    df = df[df["downloaded"] == True]

    # Pre-index all positive files once in memory for instant O(1) lookup
    all_pos_files = {p.stem: p for p in find_audio(POSITIVE_DIR)}

    paths = []
    for _, row in df.iterrows():
        yt_id = str(row["yt_id"])
        if yt_id in all_pos_files:
            p = all_pos_files[yt_id]
            if p.name not in BAD_FILES:
                paths.append(p)

    return paths

# ============================================================
# DATASET
# ============================================================
class SirenDataset(Dataset):

    def __init__(self, samples):
        self.samples = samples

        self.mel_transform = torchaudio.transforms.MelSpectrogram(
            sample_rate=SAMPLE_RATE,
            n_fft=1024,
            hop_length=256,
            n_mels=N_MELS
        )

    def __len__(self):
        return len(self.samples)

    def load_audio(self, path):

        waveform, sr = torchaudio.load(str(path))

        # Stereo -> mono
        if waveform.shape[0] > 1:
            waveform = waveform.mean(dim=0, keepdim=True)

        # Resample
        if sr != SAMPLE_RATE:
            waveform = torchaudio.functional.resample(
                waveform,
                sr,
                SAMPLE_RATE
            )

        # 10 seconds
        if waveform.shape[1] < TARGET_SAMPLES:
            waveform = F.pad(
                waveform,
                (0, TARGET_SAMPLES - waveform.shape[1])
            )
        else:
            waveform = waveform[:, :TARGET_SAMPLES]

        return waveform

    def __getitem__(self, index):

        path, label = self.samples[index]

        try:

            waveform = self.load_audio(path)

            mel = self.mel_transform(waveform)

            mel = torch.log(mel + 1e-6)

            mel = F.interpolate(
                mel.unsqueeze(0),
                size=(IMAGE_SIZE, IMAGE_SIZE),
                mode="bilinear",
                align_corners=False
            ).squeeze(0)

            return mel, torch.tensor([label], dtype=torch.float32)

        except Exception as e:

            print("Audio error:", path)
            print(e)

            new_index = (index + 1) % len(self.samples)

            return self.__getitem__(new_index)

# ============================================================
# BUILD DATASET
# ============================================================
print("=" * 70)
print("LIGHT SIREN - REAL WORLD FINE-TUNING")
print("=" * 70)

print("Searching AudioSet positives...")

positive_files = get_audioset_positive()

print("AudioSet positive files:", len(positive_files))

print("Searching AudioSet negatives...")

negative_files = find_audio(NEGATIVE_DIR)

random.shuffle(negative_files)

negative_files = negative_files[:len(positive_files)]

print("AudioSet negative files:", len(negative_files))

real_world_files = find_audio(REAL_WORLD_DIR)
print("Real-world positive windows:", len(real_world_files))

real_world_neg_files = find_audio(REAL_WORLD_NEG_DIR) if REAL_WORLD_NEG_DIR.exists() else []
print("Real-world hard negative windows (classroom/speech):", len(real_world_neg_files))

# Balanced transfer fine-tuning:
# 800 AudioSet positives, 800 AudioSet negatives, all real sirens (oversampled 4x), all Kerala hard negatives
random.shuffle(positive_files)
random.shuffle(negative_files)
selected_pos = positive_files[:800]
selected_neg = negative_files[:800]

# ============================================================
# TRAINING DATA
# ============================================================
samples = []

for p in selected_pos:
    samples.append((p, 1))

for p in selected_neg:
    samples.append((p, 0))

# Oversample real-world sirens to ensure equal representation with classroom negatives
for _ in range(4):
    for p in real_world_files:
        samples.append((p, 1))

# All real-world Kerala hard negatives (classroom chatter, Indian school, auto-rickshaw, traffic)
for p in real_world_neg_files:
    samples.append((p, 0))

random.shuffle(samples)

print()
print("BALANCED FINE-TUNING DATA")
print("-" * 70)
print("AudioSet positive:", len(selected_pos))
print("AudioSet negative:", len(selected_neg))
print("Real-world positive (oversampled 4x):", len(real_world_files) * 4)
print("Kerala hard negatives (classroom/traffic):", len(real_world_neg_files))
print("TOTAL SAMPLES:", len(samples))

# ============================================================
# DATA LOADER
# ============================================================
dataset = SirenDataset(samples)

loader = DataLoader(
    dataset,
    batch_size=BATCH_SIZE,
    shuffle=True,
    num_workers=0,
    pin_memory=torch.cuda.is_available()
)

# ============================================================
# MODEL & PRETRAINED TRANSFER
# ============================================================
model = LightSirenCNN().to(device)

if BEST_PATH.exists():
    print(f"\n[FINE-TUNE] Loading existing pretrained weights from {BEST_PATH.name}...")
    try:
        ckpt = torch.load(BEST_PATH, map_location=device)
        model.load_state_dict(ckpt["model_state_dict"])
        print("[FINE-TUNE] Successfully initialized with base siren detection knowledge.")
    except Exception as e:
        print("[!] Warning: Could not load base checkpoint, starting fresh:", e)

criterion = nn.BCEWithLogitsLoss()

# Fine-tuning learning rate (gentle to preserve general audio features while fitting classroom negatives)
optimizer = torch.optim.Adam(
    model.parameters(),
    lr=2e-4
)

best_loss = float("inf")

print()
print("Device:", device)

if torch.cuda.is_available():
    print("GPU:", torch.cuda.get_device_name(0))

print()
print("Starting training...")
print()

# ============================================================
# TRAIN
# ============================================================
for epoch in range(EPOCHS):

    model.train()

    running_loss = 0.0
    correct = 0
    total = 0

    for inputs, labels in loader:

        inputs = inputs.to(device, non_blocking=True)
        labels = labels.to(device, non_blocking=True)

        optimizer.zero_grad()

        outputs = model(inputs)

        loss = criterion(outputs, labels)

        loss.backward()

        optimizer.step()

        running_loss += loss.item() * inputs.size(0)

        predictions = (torch.sigmoid(outputs) >= 0.5).float()

        correct += (predictions == labels).sum().item()
        total += labels.numel()

    epoch_loss = running_loss / total
    epoch_acc = correct / total

    print(
        f"Epoch {epoch+1:02d}/{EPOCHS} | "
        f"Loss {epoch_loss:.4f} | "
        f"Train Acc {epoch_acc:.4f}"
    )

    if epoch_loss < best_loss:

        best_loss = epoch_loss

        torch.save(
            {
                "model_state_dict": model.state_dict(),
                "best_loss": best_loss,
                "epoch": epoch + 1,
                "real_world_windows": len(real_world_files)
            },
            BEST_PATH
        )

        print("  -> Best model saved.")

print()
print("=" * 70)
print("TRAINING COMPLETE")
print("=" * 70)
print("Best training loss:", best_loss)
print("Checkpoint:", BEST_PATH)

# ============================================================
# EXPORT BEST MODEL TO ONNX
# ============================================================
print("\nExporting best model to ONNX...")
checkpoint = torch.load(BEST_PATH, map_location="cpu")
model.load_state_dict(checkpoint["model_state_dict"])
model.eval()
model.to("cpu")

dummy_input = torch.randn(1, 1, IMAGE_SIZE, IMAGE_SIZE)
onnx_path = Path(__file__).resolve().parent / "light_siren_realworld_v2.onnx"

torch.onnx.export(
    model,
    dummy_input,
    str(onnx_path),
    input_names=["mel_spectrogram"],
    output_names=["logit"],
    opset_version=13
)
print("ONNX model exported successfully:", onnx_path)
print("=" * 70)
