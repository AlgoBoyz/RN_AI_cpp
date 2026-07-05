"""
生成 MNIST CNN ONNX 模型，用于弹药数字识别。
约 1.5MB，推理 <1ms。

依赖: pip install torch torchvision
"""

import torch
import torch.nn as nn

class MNISTNet(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(1, 16, 3, padding=1)
        self.conv2 = nn.Conv2d(16, 32, 3, padding=1)
        self.pool = nn.MaxPool2d(2, 2)
        self.fc1 = nn.Linear(32 * 7 * 7, 64)
        self.fc2 = nn.Linear(64, 10)

    def forward(self, x):
        x = self.pool(torch.relu(self.conv1(x)))
        x = self.pool(torch.relu(self.conv2(x)))
        x = x.view(x.size(0), -1)
        x = torch.relu(self.fc1(x))
        x = self.fc2(x)
        return x


def export_onnx(output_path="models/mnist.onnx"):
    model = MNISTNet()

    # 加载 MNIST 预训练权重（或从零训练一个简单的）
    # 为了轻量，直接用随机权重演示结构
    # 实际使用前请用真实数字模板微调
    dummy = torch.randn(1, 1, 28, 28)
    model.eval()

    torch.onnx.export(
        model,
        dummy,
        output_path,
        input_names=["input"],
        output_names=["output"],
        dynamic_axes={"input": {0: "batch"}, "output": {0: "batch"}},
        opset_version=11,
    )
    print(f"ONNX model saved to {output_path} ({os.path.getsize(output_path)/1024:.1f} KB)")


def train_mnist(output_path="models/mnist.onnx"):
    """从零训练 MNIST 并导出 ONNX（需要 torchvision）"""
    from torchvision import datasets, transforms
    from torch.utils.data import DataLoader
    import os

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Training on {device}...")

    transform = transforms.Compose([
        transforms.ToTensor(),
        transforms.Normalize((0.1307,), (0.3081,))
    ])

    train_loader = DataLoader(
        datasets.MNIST("./data", train=True, download=True, transform=transform),
        batch_size=64, shuffle=True
    )

    model = MNISTNet().to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=0.001)
    criterion = nn.CrossEntropyLoss()

    model.train()
    for epoch in range(3):
        total_loss = 0
        for batch_idx, (data, target) in enumerate(train_loader):
            data, target = data.to(device), target.to(device)
            optimizer.zero_grad()
            output = model(data)
            loss = criterion(output, target)
            loss.backward()
            optimizer.step()
            total_loss += loss.item()
        print(f"  Epoch {epoch+1}: loss={total_loss/len(train_loader):.4f}")

    model.eval()
    model.cpu()
    dummy = torch.randn(1, 1, 28, 28)
    torch.onnx.export(
        model, dummy, output_path,
        input_names=["input"], output_names=["output"],
        opset_version=11,
    )
    print(f"Model saved to {output_path} ({os.path.getsize(output_path)/1024:.1f} KB)")


if __name__ == "__main__":
    import os
    os.makedirs("models", exist_ok=True)
    train_mnist("models/mnist.onnx")
