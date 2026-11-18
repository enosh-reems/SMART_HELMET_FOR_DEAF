import os
import random
import pandas as pd
import torch
import torchaudio
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader
from light_siren_cnn import LightSirenCNN

ROOT = r".\datasets\AudioSet_EV_v2PANNs_2020"
POS_CSV = os.path.join(ROOT, "EV_Positives.csv")
POS_DIR = os.path.join(ROOT, "Positive_files")
NEG_DIR = os.path.join(ROOT, "Negative_files")

SAMPLE_RATE = 16000
NUM_SAMPLES = 16000 * 10
SEED = 42
BATCH_SIZE = 32

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
                    waveform = F.pad(
                        waveform,
                        (0, NUM_SAMPLES - waveform.shape[-1])
                    )
                else:
                    waveform = waveform[..., :NUM_SAMPLES]

                mel = self.mel(waveform)
                x = torch.log(mel + 1e-6)

                x = F.interpolate(
                    x.unsqueeze(0),
                    size=(128, 128),
                    mode="bilinear",
                    align_corners=False
                ).squeeze(0)

                return x, torch.tensor(
                    self.labels[idx],
                    dtype=torch.long
                )

            except Exception:
                idx = (idx + 1) % len(self.files)
                attempts += 1

        raise RuntimeError("Could not decode test audio.")


def find_audio(folder):
    files = []

    for root, _, names in os.walk(folder):
        for name in names:
            if name.lower().endswith(
                (".wav", ".mp3", ".flac", ".ogg")
            ):
                files.append(os.path.join(root, name))

    return files


def get_positive_files():
    df = pd.read_csv(POS_CSV)

    df = df[
        df["downloaded"].astype(str).str.lower() == "true"
    ]

    files = []

    for _, row in df.iterrows():
        yt_id = str(row["yt_id"])

        for subfolder in (
            "balanced_train",
            "eval",
            "unbalanced"
        ):
            candidate = os.path.join(
                POS_DIR,
                subfolder,
                yt_id + ".wav"
            )

            if os.path.isfile(candidate):
                files.append(candidate)
                break

    return list(dict.fromkeys(files))


def main():
    print("Preparing test dataset...")

    positive = get_positive_files()
    negative = find_audio(NEG_DIR)

    random.Random(SEED).shuffle(negative)
    negative = negative[:len(positive)]

    files = positive + negative
    labels = [1] * len(positive) + [0] * len(negative)

    combined = list(zip(files, labels))
    random.Random(SEED).shuffle(combined)

    n = len(combined)
    test = combined[int(n * 0.9):]

    test_files = [x[0] for x in test]
    test_labels = [x[1] for x in test]

    print("Test samples:", len(test_files))
    print("Positive:", sum(test_labels))
    print("Negative:", len(test_labels) - sum(test_labels))

    dataset = SirenDataset(test_files, test_labels)

    loader = DataLoader(
        dataset,
        batch_size=BATCH_SIZE,
        shuffle=False,
        num_workers=0
    )

    device = torch.device(
        "cuda" if torch.cuda.is_available() else "cpu"
    )

    model = LightSirenCNN().to(device)

    checkpoint = torch.load(
        r".\checkpoints\light_siren_realworld_v2\best.pt",
        map_location=device,
        weights_only=False
    )

    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()

    print("Loaded best validation model.")
    print("Best validation F1:",
          checkpoint["best_val_f1"])

    tp = tn = fp = fn = 0

    with torch.no_grad():
        for x, y in loader:
            x = x.to(device)

            logits = model(x)
            probabilities = torch.sigmoid(logits).flatten()

            predictions = (
                probabilities >= 0.5
            ).long().cpu()

            y = y.long()

            tp += ((predictions == 1) & (y == 1)).sum().item()
            tn += ((predictions == 0) & (y == 0)).sum().item()
            fp += ((predictions == 1) & (y == 0)).sum().item()
            fn += ((predictions == 0) & (y == 1)).sum().item()

    accuracy = (tp + tn) / max(tp + tn + fp + fn, 1)
    precision = tp / max(tp + fp, 1)
    recall = tp / max(tp + fn, 1)
    specificity = tn / max(tn + fp, 1)

    f1 = (
        2 * precision * recall /
        max(precision + recall, 1e-8)
    )

    print()
    print("=" * 50)
    print("LIGHT SIREN TEST RESULTS")
    print("=" * 50)
    print(f"Accuracy:     {accuracy:.4f} ({accuracy*100:.2f}%)")
    print(f"Precision:    {precision:.4f} ({precision*100:.2f}%)")
    print(f"Recall:       {recall:.4f} ({recall*100:.2f}%)")
    print(f"Specificity:   {specificity:.4f} ({specificity*100:.2f}%)")
    print(f"F1:           {f1:.4f} ({f1*100:.2f}%)")
    print()
    print("Confusion Matrix")
    print(f"TN: {tn}")
    print(f"FP: {fp}")
    print(f"FN: {fn}")
    print(f"TP: {tp}")
    print("=" * 50)


if __name__ == "__main__":
    main()
