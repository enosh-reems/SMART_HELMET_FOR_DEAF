import os
import sys
import random
import pandas as pd
import torch
import torchaudio
import torch.nn as nn
from torch.utils.data import Dataset, DataLoader
from light_siren_cnn import LightSirenCNN

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))

ROOT = r".\datasets\AudioSet_EV_v2PANNs_2020"
POS_CSV = os.path.join(ROOT, "EV_Positives.csv")
POS_DIR = os.path.join(ROOT, "Positive_files")
NEG_DIR = os.path.join(ROOT, "Negative_files")

SAMPLE_RATE = 16000
CLIP_SECONDS = 10
NUM_SAMPLES = SAMPLE_RATE * CLIP_SECONDS
BATCH_SIZE = 32
EPOCHS = 15
LR = 1e-3
SEED = 42

random.seed(SEED)
torch.manual_seed(SEED)

class SirenDataset(Dataset):
    def __init__(self, files, labels):
        self.files = files
        self.labels = labels
        self.mel = torchaudio.transforms.MelSpectrogram(
            sample_rate=SAMPLE_RATE,
            n_fft=512,
            hop_length=256,
            n_mels=64
        )

    def __len__(self):
        return len(self.files)

    def __getitem__(self, idx):
        attempts = 0

        while attempts < 20:
            try:
                waveform, sr = torchaudio.load(self.files[idx])

                if waveform.shape[0] > 1:
                    waveform = waveform.mean(dim=0, keepdim=True)

                if sr != SAMPLE_RATE:
                    waveform = torchaudio.functional.resample(
                        waveform, sr, SAMPLE_RATE
                    )

                if waveform.shape[-1] < NUM_SAMPLES:
                    waveform = torch.nn.functional.pad(
                        waveform, (0, NUM_SAMPLES - waveform.shape[-1])
                    )
                else:
                    waveform = waveform[..., :NUM_SAMPLES]

                mel = self.mel(waveform)
                x = torch.log(mel + 1e-6)

                x = torch.nn.functional.interpolate(
                    x.unsqueeze(0),
                    size=(128, 128),
                    mode="bilinear",
                    align_corners=False
                ).squeeze(0)

                return x, torch.tensor(
                    [self.labels[idx]],
                    dtype=torch.float32
                )

            except Exception:
                idx = (idx + 1) % len(self.files)
                attempts += 1

        raise RuntimeError(
            "Could not decode 20 consecutive audio files."
        )



def find_audio(folder):
    files = []
    for root, _, names in os.walk(folder):
        for name in names:
            if name.lower().endswith((".wav", ".mp3", ".flac", ".ogg")):
                files.append(os.path.join(root, name))
    return files


def get_positive_files():
    df = pd.read_csv(POS_CSV)

    df = df[df["downloaded"].astype(str).str.lower() == "true"]

    files = []

    for _, row in df.iterrows():
        yt_id = str(row["yt_id"])

        found = []
        for subfolder in ("balanced_train", "eval", "unbalanced"):
            candidate = os.path.join(
                POS_DIR, subfolder, yt_id + ".wav"
            )
            if os.path.isfile(candidate):
                found.append(candidate)

        if found:
            files.append(found[0])

    return list(dict.fromkeys(files))


def get_negative_files():
    files = find_audio(NEG_DIR)

    # The existing project balances negatives to the positive count.
    random.Random(SEED).shuffle(files)

    return files


def split_balanced(files, labels):
    combined = list(zip(files, labels))
    random.Random(SEED).shuffle(combined)

    n = len(combined)

    train_end = int(n * 0.8)
    val_end = int(n * 0.9)

    train = combined[:train_end]
    val = combined[train_end:val_end]
    test = combined[val_end:]

    return (
        [x[0] for x in train], [x[1] for x in train],
        [x[0] for x in val], [x[1] for x in val],
        [x[0] for x in test], [x[1] for x in test]
    )


def main():
    print("Loading dataset...")

    positive = get_positive_files()
    negative = get_negative_files()

    print("Detected positive files:", len(positive))
    print("Detected negative files:", len(negative))

    if len(positive) == 0:
        raise RuntimeError("No positive files found.")

    if len(negative) < len(positive):
        raise RuntimeError("Not enough negative files.")

    negative = negative[:len(positive)]

    files = positive + negative
    labels = [1] * len(positive) + [0] * len(negative)

    train_files, train_labels, val_files, val_labels, test_files, test_labels = \
        split_balanced(files, labels)

    print()
    print("FINAL BALANCED DATASET")
    print("Total:", len(files))
    print("Positive:", sum(labels))
    print("Negative:", len(labels) - sum(labels))

    print()
    print("Train:", len(train_files),
          "Positive:", sum(train_labels),
          "Negative:", len(train_labels) - sum(train_labels))

    print("Validation:", len(val_files),
          "Positive:", sum(val_labels),
          "Negative:", len(val_labels) - sum(val_labels))

    print("Test:", len(test_files),
          "Positive:", sum(test_labels),
          "Negative:", len(test_labels) - sum(test_labels))

    train_ds = SirenDataset(train_files, train_labels)
    val_ds = SirenDataset(val_files, val_labels)

    train_loader = DataLoader(
        train_ds,
        batch_size=BATCH_SIZE,
        shuffle=True,
        num_workers=0,
        pin_memory=True
    )

    val_loader = DataLoader(
        val_ds,
        batch_size=BATCH_SIZE,
        shuffle=False,
        num_workers=0,
        pin_memory=True
    )

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    model = LightSirenCNN().to(device)

    print()
    print("Device:", device)

    if torch.cuda.is_available():
        print("GPU:", torch.cuda.get_device_name(0))

    criterion = nn.BCEWithLogitsLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=LR)

    best_f1 = 0.0

    os.makedirs("./checkpoints/light_siren", exist_ok=True)

    for epoch in range(EPOCHS):
        model.train()

        total_loss = 0.0
        tp = tn = fp = fn = 0

        for x, y in train_loader:
            x = x.to(device, non_blocking=True)
            y = y.to(device, non_blocking=True)

            optimizer.zero_grad()

            logits = model(x)
            loss = criterion(logits, y)

            loss.backward()
            optimizer.step()

            total_loss += loss.item()

            pred = (torch.sigmoid(logits) >= 0.5).float()

            tp += ((pred == 1) & (y == 1)).sum().item()
            tn += ((pred == 0) & (y == 0)).sum().item()
            fp += ((pred == 1) & (y == 0)).sum().item()
            fn += ((pred == 0) & (y == 1)).sum().item()

        train_precision = tp / max(tp + fp, 1)
        train_recall = tp / max(tp + fn, 1)
        train_f1 = 2 * train_precision * train_recall / max(
            train_precision + train_recall, 1e-8
        )

        model.eval()

        vtp = vtn = vfp = vfn = 0
        val_loss = 0.0

        with torch.no_grad():
            for x, y in val_loader:
                x = x.to(device, non_blocking=True)
                y = y.to(device, non_blocking=True)

                logits = model(x)
                loss = criterion(logits, y)
                val_loss += loss.item()

                pred = (torch.sigmoid(logits) >= 0.5).float()

                vtp += ((pred == 1) & (y == 1)).sum().item()
                vtn += ((pred == 0) & (y == 0)).sum().item()
                vfp += ((pred == 1) & (y == 0)).sum().item()
                vfn += ((pred == 0) & (y == 1)).sum().item()

        precision = vtp / max(vtp + vfp, 1)
        recall = vtp / max(vtp + vfn, 1)
        f1 = 2 * precision * recall / max(precision + recall, 1e-8)
        accuracy = (vtp + vtn) / max(vtp + vtn + vfp + vfn, 1)

        print(
            f"Epoch {epoch+1:02d}/{EPOCHS} | "
            f"Train Loss {total_loss/len(train_loader):.4f} | "
            f"Train F1 {train_f1:.4f} | "
            f"Val Loss {val_loss/len(val_loader):.4f} | "
            f"Val Acc {accuracy:.4f} | "
            f"Val Precision {precision:.4f} | "
            f"Val Recall {recall:.4f} | "
            f"Val F1 {f1:.4f}"
        )

        if f1 > best_f1:
            best_f1 = f1

            torch.save(
                {
                    "model_state_dict": model.state_dict(),
                    "best_val_f1": best_f1,
                    "sample_rate": SAMPLE_RATE,
                    "n_mels": 64,
                    "input_size": 128,
                },
                "./checkpoints/light_siren/best.pt"
            )

            print("  -> Best model saved.")

    print()
    print("TRAINING COMPLETE")
    print("Best validation F1:", best_f1)
    print("Checkpoint: ./checkpoints/light_siren/best.pt")


if __name__ == "__main__":
    main()
