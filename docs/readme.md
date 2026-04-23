## Dependencies
# Audio libs
```bash
sudo apt-get install librtaudio-dev -y
```
# ONNX-Sherpa on linux
```bash
git clone https://github.com/k2-fsa/sherpa-onnx
cd sherpa-onnx
mkdir build
cd build

cmake \
  -DSHERPA_ONNX_ENABLE_C_API=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=ON \
  -DCMAKE_INSTALL_PREFIX=/home/operador/Documents/tmp/resources/sherpa-onnx/shared \
  ..

make -j$(nproc)
make install
```
# OpenVino x x86 MicroProcessor
# Compilation
```bash
git clone -b 2026.1.0 https://github.com/openvinotoolkit/openvino.git
cd openvino
git submodule update --init --recursive
sudo ./install_build_dependencies.sh
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
```
# apt
```bash
wget https://apt.repos.intel.com/intel-gpg-keys/GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
sudo apt-key add GPG-PUB-KEY-INTEL-SW-PRODUCTS.PUB
echo "deb https://apt.repos.intel.com/openvino ubuntu20 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list
echo "deb https://apt.repos.intel.com/openvino ubuntu22 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list
echo "deb https://apt.repos.intel.com/openvino ubuntu24 main" | sudo tee /etc/apt/sources.list.d/intel-openvino.list
sudo apt update
apt-cache search openvino
sudo apt install openvino-2026.1.0
```
# configure OpenVino for ORT
1.- Add to ~/.bashrc
```bash
export OpenVINO_DIR=/usr/lib/cmake/openvino2026.1.0
```

2.- Now install pre-compiled packages
```bash
#first activate your virtualenv
pip install onnxruntime-openvino==1.24.1


```
