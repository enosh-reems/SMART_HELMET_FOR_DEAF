import torch
import torch.nn as nn

class LightSirenCNN(nn.Module):
    def __init__(self):
        super().__init__()

        self.features = nn.Sequential(
            nn.Conv2d(1, 16, 3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(2),

            nn.Conv2d(16, 32, 3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.MaxPool2d(2),

            nn.Conv2d(32, 64, 3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(),
            nn.MaxPool2d(2),

            nn.Conv2d(64, 96, 3, padding=1),
            nn.BatchNorm2d(96),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d((1, 1))
        )

        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(96, 32),
            nn.ReLU(),
            nn.Dropout(0.2),
            nn.Linear(32, 1)
        )

    def forward(self, x):
        x = self.features(x)
        return self.classifier(x)

if __name__ == "__main__":
    model = LightSirenCNN()
    x = torch.randn(1, 1, 128, 128)
    y = model(x)

    params = sum(p.numel() for p in model.parameters())
    size_mb = params * 4 / (1024 * 1024)

    print("MODEL LOAD: SUCCESS")
    print("Input:", tuple(x.shape))
    print("Output:", tuple(y.shape))
    print("Parameters:", params)
    print("FP32 weight size: %.3f MB" % size_mb)
