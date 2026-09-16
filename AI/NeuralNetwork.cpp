/**
 * Potato Engine Neural Network Implementation
 */

#include "NeuralNetwork.h"
#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace Potato {
namespace AI {

// ============================================================================
// Activation Functions
// ============================================================================

std::vector<float> Activation::Softmax(const std::vector<float>& x) {
    std::vector<float> result(x.size());
    
    // Find max for numerical stability
    float maxVal = *std::max_element(x.begin(), x.end());
    
    // Compute exp(x - max) and sum
    float sum = 0.0f;
    for (size_t i = 0; i < x.size(); i++) {
        result[i] = std::exp(x[i] - maxVal);
        sum += result[i];
    }
    
    // Normalize
    for (size_t i = 0; i < result.size(); i++) {
        result[i] /= sum;
    }
    
    return result;
}

// ============================================================================
// Loss Functions
// ============================================================================

float Loss::MSE(const std::vector<float>& predicted, const std::vector<float>& target) {
    float sum = 0.0f;
    for (size_t i = 0; i < predicted.size(); i++) {
        float diff = predicted[i] - target[i];
        sum += diff * diff;
    }
    return sum / static_cast<float>(predicted.size());
}

std::vector<float> Loss::MSEGradient(const std::vector<float>& predicted, 
                                       const std::vector<float>& target) {
    std::vector<float> gradient(predicted.size());
    for (size_t i = 0; i < predicted.size(); i++) {
        gradient[i] = 2.0f * (predicted[i] - target[i]) / static_cast<float>(predicted.size());
    }
    return gradient;
}

float Loss::CrossEntropy(const std::vector<float>& predicted, const std::vector<float>& target) {
    float sum = 0.0f;
    for (size_t i = 0; i < predicted.size(); i++) {
        sum -= target[i] * std::log(predicted[i] + 1e-10f);
    }
    return sum;
}

std::vector<float> Loss::CrossEntropyGradient(const std::vector<float>& predicted,
                                                const std::vector<float>& target) {
    std::vector<float> gradient(predicted.size());
    for (size_t i = 0; i < predicted.size(); i++) {
        gradient[i] = -target[i] / (predicted[i] + 1e-10f);
    }
    return gradient;
}

float Loss::BinaryCrossEntropy(float predicted, float target) {
    return -(target * std::log(predicted + 1e-10f) + 
             (1.0f - target) * std::log(1.0f - predicted + 1e-10f));
}

float Loss::BinaryCrossEntropyGradient(float predicted, float target) {
    return -(target / (predicted + 1e-10f) - (1.0f - target) / (1.0f - predicted + 1e-10f));
}

// ============================================================================
// Neural Layer
// ============================================================================

NeuralLayer::NeuralLayer(size_t inputSize, size_t outputSize, const std::string& activation)
    : inputSize(inputSize)
    , outputSize(outputSize)
    , activationType(activation)
    , rng(std::random_device{}()) {
    
    // Resize weights and biases
    weights.resize(outputSize, std::vector<float>(inputSize));
    biases.resize(outputSize);
    
    // Set activation function
    if (activation == "sigmoid") {
        this->activation = Activation::Sigmoid;
        activationDerivative = Activation::SigmoidDerivative;
    } else if (activation == "relu") {
        this->activation = Activation::ReLU;
        activationDerivative = Activation::ReLUDerivative;
    } else if (activation == "tanh") {
        this->activation = Activation::Tanh;
        activationDerivative = Activation::TanhDerivative;
    } else if (activation == "leaky_relu") {
        this->activation = [](float x) { return Activation::LeakyReLU(x); };
        activationDerivative = [](float x) { return Activation::LeakyReLUDerivative(x); };
    } else {
        this->activation = Activation::ReLU;
        activationDerivative = Activation::ReLUDerivative;
    }
    
    // Initialize weights
    InitializeWeights();
}

std::vector<float> NeuralLayer::Forward(const std::vector<float>& input) {
    lastInput = input;
    lastOutput.resize(outputSize);
    
    for (size_t i = 0; i < outputSize; i++) {
        float sum = biases[i];
        for (size_t j = 0; j < inputSize; j++) {
            sum += weights[i][j] * input[j];
        }
        lastOutput[i] = activation(sum);
    }
    
    return lastOutput;
}

std::vector<float> NeuralLayer::Backward(const std::vector<float>& gradient, float learningRate) {
    std::vector<float> inputGradient(inputSize, 0.0f);
    
    for (size_t i = 0; i < outputSize; i++) {
        float delta = gradient[i] * activationDerivative(lastOutput[i]);
        
        // Update weights
        for (size_t j = 0; j < inputSize; j++) {
            inputGradient[j] += weights[i][j] * delta;
            weights[i][j] -= learningRate * delta * lastInput[j];
        }
        
        // Update bias
        biases[i] -= learningRate * delta;
    }
    
    return inputGradient;
}

void NeuralLayer::SetWeights(const std::vector<std::vector<float>>& newWeights) {
    weights = newWeights;
}

void NeuralLayer::SetBiases(const std::vector<float>& newBiases) {
    biases = newBiases;
}

void NeuralLayer::InitializeWeights(float scale) {
    std::normal_distribution<float> dist(0.0f, scale);
    
    for (size_t i = 0; i < outputSize; i++) {
        for (size_t j = 0; j < inputSize; j++) {
            weights[i][j] = dist(rng);
        }
        biases[i] = 0.0f;
    }
}

// ============================================================================
// Neural Network
// ============================================================================

NeuralNetwork::NeuralNetwork()
    : built(false)
    , lossFunction("mse") {
    InitializeLossFunction();
}

void NeuralNetwork::AddLayer(size_t size, const std::string& activation) {
    if (built) {
        throw std::runtime_error("Cannot add layers after network is built");
    }
    layerSizes.push_back(size);
    layerActivations.push_back(activation);
}

void NeuralNetwork::Build() {
    if (layerSizes.size() < 2) {
        throw std::runtime_error("Network must have at least 2 layers (input and output)");
    }
    
    for (size_t i = 0; i < layerSizes.size() - 1; i++) {
        size_t inputSize = layerSizes[i];
        size_t outputSize = layerSizes[i + 1];
        std::string activation = layerActivations[i];
        
        layers.push_back(std::make_unique<NeuralLayer>(inputSize, outputSize, activation));
    }
    
    built = true;
}

std::vector<float> NeuralNetwork::Forward(const std::vector<float>& input) {
    if (!built) {
        throw std::runtime_error("Network must be built before forward pass");
    }
    
    std::vector<float> current = input;
    
    for (auto& layer : layers) {
        current = layer->Forward(current);
    }
    
    return current;
}

float NeuralNetwork::TrainStep(const std::vector<float>& input,
                                const std::vector<float>& target,
                                float learningRate) {
    // Forward pass
    std::vector<float> predicted = Forward(input);
    
    // Compute loss gradient
    std::vector<float> gradient = lossGradientFn(predicted, target);
    
    // Backward pass
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; i--) {
        gradient = layers[i]->Backward(gradient, learningRate);
    }
    
    // Compute loss
    return lossFn(predicted, target);
}

void NeuralNetwork::Train(const std::vector<std::vector<float>>& inputs,
                          const std::vector<std::vector<float>>& targets,
                          size_t epochs,
                          float learningRate,
                          size_t batchSize) {
    if (!built) {
        throw std::runtime_error("Network must be built before training");
    }
    
    if (inputs.size() != targets.size()) {
        throw std::runtime_error("Inputs and targets must have the same size");
    }
    
    size_t datasetSize = inputs.size();
    
    for (size_t epoch = 0; epoch < epochs; epoch++) {
        float totalLoss = 0.0f;
        
        // Shuffle indices
        std::vector<size_t> indices(datasetSize);
        for (size_t i = 0; i < datasetSize; i++) indices[i] = i;
        std::shuffle(indices.begin(), indices.end(), std::mt19937(std::random_device{}()));
        
        // Mini-batch training
        for (size_t i = 0; i < datasetSize; i += batchSize) {
            size_t batchEnd = std::min(i + batchSize, datasetSize);
            
            for (size_t j = i; j < batchEnd; j++) {
                size_t idx = indices[j];
                float loss = TrainStep(inputs[idx], targets[idx], learningRate);
                totalLoss += loss;
            }
        }
        
        float avgLoss = totalLoss / static_cast<float>(datasetSize);
        
        // Could add logging here
        if (epoch % 100 == 0) {
            // Log progress
        }
    }
}

std::vector<float> NeuralNetwork::Predict(const std::vector<float>& input) {
    return Forward(input);
}

int NeuralNetwork::PredictClass(const std::vector<float>& input) {
    std::vector<float> output = Forward(input);
    return std::distance(output.begin(), 
                        std::max_element(output.begin(), output.end()));
}

float NeuralNetwork::Evaluate(const std::vector<std::vector<float>>& inputs,
                               const std::vector<std::vector<float>>& targets) {
    float totalLoss = 0.0f;
    
    for (size_t i = 0; i < inputs.size(); i++) {
        std::vector<float> predicted = Forward(inputs[i]);
        totalLoss += lossFn(predicted, targets[i]);
    }
    
    return totalLoss / static_cast<float>(inputs.size());
}

std::string NeuralNetwork::Serialize() const {
    std::stringstream ss;
    
    ss << built << "\n";
    ss << lossFunction << "\n";
    ss << layerSizes.size() << "\n";
    
    for (size_t size : layerSizes) {
        ss << size << " ";
    }
    ss << "\n";
    
    for (size_t i = 0; i < layers.size(); i++) {
        const auto& layer = layers[i];
        const auto& weights = layer->GetWeights();
        const auto& biases = layer->GetBiases();
        
        ss << layer->GetInputSize() << " " << layer->GetOutputSize() << "\n";
        
        for (const auto& row : weights) {
            for (float w : row) {
                ss << w << " ";
            }
            ss << "\n";
        }
        
        for (float b : biases) {
            ss << b << " ";
        }
        ss << "\n";
    }
    
    return ss.str();
}

bool NeuralNetwork::Deserialize(const std::string& data) {
    std::stringstream ss(data);
    
    ss >> built;
    ss >> lossFunction;
    
    size_t numLayers;
    ss >> numLayers;
    
    layerSizes.resize(numLayers);
    for (size_t i = 0; i < numLayers; i++) {
        ss >> layerSizes[i];
    }
    
    // Rebuild network
    layers.clear();
    for (size_t i = 0; i < layerSizes.size() - 1; i++) {
        size_t inputSize = layerSizes[i];
        size_t outputSize = layerSizes[i + 1];
        layers.push_back(std::make_unique<NeuralLayer>(inputSize, outputSize, "relu"));
    }
    
    // Load weights
    for (size_t i = 0; i < layers.size(); i++) {
        size_t inputSize, outputSize;
        ss >> inputSize >> outputSize;
        
        std::vector<std::vector<float>> weights(outputSize, std::vector<float>(inputSize));
        for (size_t j = 0; j < outputSize; j++) {
            for (size_t k = 0; k < inputSize; k++) {
                ss >> weights[j][k];
            }
        }
        
        std::vector<float> biases(outputSize);
        for (size_t j = 0; j < outputSize; j++) {
            ss >> biases[j];
        }
        
        layers[i]->SetWeights(weights);
        layers[i]->SetBiases(biases);
    }
    
    InitializeLossFunction();
    return true;
}

void NeuralNetwork::CopyWeightsFrom(const NeuralNetwork& other) {
    if (layers.size() != other.layers.size()) {
        throw std::runtime_error("Cannot copy weights: layer count mismatch");
    }
    
    for (size_t i = 0; i < layers.size(); i++) {
        layers[i]->SetWeights(other.layers[i]->GetWeights());
        layers[i]->SetBiases(other.layers[i]->GetBiases());
    }
}

void NeuralNetwork::SetLossFunction(const std::string& lossType) {
    lossFunction = lossType;
    InitializeLossFunction();
}

void NeuralNetwork::InitializeLossFunction() {
    if (lossFunction == "mse") {
        lossFn = Loss::MSE;
        lossGradientFn = Loss::MSEGradient;
    } else if (lossFunction == "cross_entropy") {
        lossFn = Loss::CrossEntropy;
        lossGradientFn = Loss::CrossEntropyGradient;
    } else {
        lossFn = Loss::MSE;
        lossGradientFn = Loss::MSEGradient;
    }
}

// ============================================================================
// ConvLayer
// ============================================================================

ConvLayer::ConvLayer(size_t inputChannels, size_t outputChannels,
                     size_t kernelSize, size_t stride, size_t padding)
    : inputChannels(inputChannels)
    , outputChannels(outputChannels)
    , kernelSize(kernelSize)
    , stride(stride)
    , padding(padding) {
    
    // Initialize kernels
    kernels.resize(outputChannels);
    for (size_t oc = 0; oc < outputChannels; oc++) {
        kernels[oc].resize(inputChannels);
        for (size_t ic = 0; ic < inputChannels; ic++) {
            kernels[oc][ic].resize(kernelSize, std::vector<float>(kernelSize));
        }
    }
    
    biases.resize(outputChannels, 0.0f);
}

std::vector<std::vector<std::vector<float>>> ConvLayer::Forward(
    const std::vector<std::vector<std::vector<float>>>& input) {
    
    // Simplified implementation - actual implementation would compute convolution
    // This is a placeholder for the full convolution operation
    
    size_t outputHeight = (input[0].size() + 2 * padding - kernelSize) / stride + 1;
    size_t outputWidth = (input[0][0].size() + 2 * padding - kernelSize) / stride + 1;
    
    std::vector<std::vector<std::vector<float>>> output(
        outputChannels,
        std::vector<std::vector<float>>(outputHeight, std::vector<float>(outputWidth, 0.0f))
    );
    
    return output;
}

std::vector<std::vector<std::vector<float>>> ConvLayer::Backward(
    const std::vector<std::vector<std::vector<float>>>& gradient,
    float learningRate) {
    
    // Placeholder for backward pass
    return gradient;
}

// ============================================================================
// PoolLayer
// ============================================================================

PoolLayer::PoolLayer(size_t poolSize, size_t stride, PoolType type)
    : poolSize(poolSize)
    , stride(stride)
    , type(type) {
}

std::vector<std::vector<std::vector<float>>> PoolLayer::Forward(
    const std::vector<std::vector<std::vector<float>>>& input) {
    
    // Placeholder for pooling operation
    return input;
}

std::vector<std::vector<std::vector<float>>> PoolLayer::Backward(
    const std::vector<std::vector<std::vector<float>>>& gradient) {
    
    // Placeholder for backward pass
    return gradient;
}

// ============================================================================
// LSTMLayer
// ============================================================================

LSTMLayer::LSTMLayer(size_t inputSize, size_t hiddenSize)
    : inputSize(inputSize)
    , hiddenSize(hiddenSize) {
    
    hiddenState.resize(hiddenSize, 0.0f);
    cellState.resize(hiddenSize, 0.0f);
    
    // Initialize weights
    Wf.resize(hiddenSize, std::vector<float>(inputSize + hiddenSize));
    Wi.resize(hiddenSize, std::vector<float>(inputSize + hiddenSize));
    Wo.resize(hiddenSize, std::vector<float>(inputSize + hiddenSize));
    Wc.resize(hiddenSize, std::vector<float>(inputSize + hiddenSize));
    
    bf.resize(hiddenSize, 0.0f);
    bi.resize(hiddenSize, 0.0f);
    bo.resize(hiddenSize, 0.0f);
    bc.resize(hiddenSize, 0.0f);
}

std::vector<float> LSTMLayer::Forward(const std::vector<float>& input, bool resetHidden) {
    if (resetHidden) {
        ResetHiddenState();
    }
    
    // Placeholder for LSTM forward pass
    return hiddenState;
}

std::vector<float> LSTMLayer::Backward(const std::vector<float>& gradient, float learningRate) {
    // Placeholder for LSTM backward pass
    return std::vector<float>(inputSize, 0.0f);
}

void LSTMLayer::ResetHiddenState() {
    std::fill(hiddenState.begin(), hiddenState.end(), 0.0f);
    std::fill(cellState.begin(), cellState.end(), 0.0f);
}

// ============================================================================
// AttentionLayer
// ============================================================================

AttentionLayer::AttentionLayer(size_t inputSize, size_t attentionSize)
    : inputSize(inputSize)
    , attentionSize(attentionSize) {
    
    Wq.resize(attentionSize, std::vector<float>(inputSize));
    Wk.resize(attentionSize, std::vector<float>(inputSize));
    Wv.resize(attentionSize, std::vector<float>(inputSize));
    
    bq.resize(attentionSize, 0.0f);
    bk.resize(attentionSize, 0.0f);
    bv.resize(attentionSize, 0.0f);
}

std::vector<float> AttentionLayer::Forward(const std::vector<std::vector<float>>& inputs) {
    // Placeholder for attention mechanism
    return std::vector<float>(attentionSize, 0.0f);
}

std::vector<std::vector<float>> AttentionLayer::Backward(
    const std::vector<std::vector<float>>& gradient,
    float learningRate) {
    
    // Placeholder for backward pass
    return gradient;
}

} // namespace AI
} // namespace Potato
