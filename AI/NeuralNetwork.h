/**
 * Potato Engine Neural Network Module
 * Simple neural network implementation for AI agents
 */

#pragma once

#include <vector>
#include <functional>
#include <memory>
#include <random>
#include <cmath>

namespace Potato {
namespace AI {

/**
 * Activation functions
 */
namespace Activation {
    // Sigmoid: 1 / (1 + e^(-x))
    inline float Sigmoid(float x) {
        return 1.0f / (1.0f + std::exp(-x));
    }
    
    // Sigmoid derivative
    inline float SigmoidDerivative(float x) {
        float s = Sigmoid(x);
        return s * (1.0f - s);
    }
    
    // ReLU: max(0, x)
    inline float ReLU(float x) {
        return std::max(0.0f, x);
    }
    
    // ReLU derivative
    inline float ReLUDerivative(float x) {
        return x > 0.0f ? 1.0f : 0.0f;
    }
    
    // Tanh: tanh(x)
    inline float Tanh(float x) {
        return std::tanh(x);
    }
    
    // Tanh derivative
    inline float TanhDerivative(float x) {
        float t = std::tanh(x);
        return 1.0f - t * t;
    }
    
    // Leaky ReLU
    inline float LeakyReLU(float x, float alpha = 0.01f) {
        return x > 0.0f ? x : alpha * x;
    }
    
    // Leaky ReLU derivative
    inline float LeakyReLUDerivative(float x, float alpha = 0.01f) {
        return x > 0.0f ? 1.0f : alpha;
    }
    
    // Softmax (for output layer)
    std::vector<float> Softmax(const std::vector<float>& x);
}

/**
 * Loss functions
 */
namespace Loss {
    // Mean Squared Error
    float MSE(const std::vector<float>& predicted, const std::vector<float>& target);
    std::vector<float> MSEGradient(const std::vector<float>& predicted, const std::vector<float>& target);
    
    // Cross Entropy (for classification)
    float CrossEntropy(const std::vector<float>& predicted, const std::vector<float>& target);
    std::vector<float> CrossEntropyGradient(const std::vector<float>& predicted, const std::vector<float>& target);
    
    // Binary Cross Entropy
    float BinaryCrossEntropy(float predicted, float target);
    float BinaryCrossEntropyGradient(float predicted, float target);
}

/**
 * Neural Network Layer
 */
class NeuralLayer {
public:
    NeuralLayer(size_t inputSize, size_t outputSize, 
                const std::string& activation = "relu");
    
    // Forward pass
    std::vector<float> Forward(const std::vector<float>& input);
    
    // Backward pass
    std::vector<float> Backward(const std::vector<float>& gradient, float learningRate);
    
    // Getters
    size_t GetInputSize() const { return inputSize; }
    size_t GetOutputSize() const { return outputSize; }
    const std::vector<std::vector<float>>& GetWeights() const { return weights; }
    const std::vector<float>& GetBiases() const { return biases; }
    
    // Setters
    void SetWeights(const std::vector<std::vector<float>>& newWeights);
    void SetBiases(const std::vector<float>& newBiases);

    // 激活類型字串（"relu"/"sigmoid"/"tanh"/"leaky_relu"/"linear"）
    const std::string& GetActivationType() const { return activationType; }
    
    // Random initialization
    void InitializeWeights(float scale = 0.01f);

    // 以固定 seed 重建權重——呼叫後本層初始化可重現
    void SetSeed(unsigned int seed);

private:
    size_t inputSize;
    size_t outputSize;
    std::vector<std::vector<float>> weights;
    std::vector<float> biases;
    std::vector<float> lastInput;
    std::vector<float> lastPreActivation;  // 激活前的加權和 z（Backward 導數需要 z 而非 y）
    std::vector<float> lastOutput;
    std::string activationType;
    
    std::function<float(float)> activation;
    std::function<float(float)> activationDerivative;
    
    std::mt19937 rng;
};

/**
 * Neural Network
 */
class NeuralNetwork {
public:
    NeuralNetwork();
    
    // Architecture
    void AddLayer(size_t size, const std::string& activation = "relu");
    void Build();
    // 以固定 seed 建構——各層權重與 Train shuffle 皆可重現
    void Build(unsigned int seed);
    
    // Forward pass
    std::vector<float> Forward(const std::vector<float>& input);
    
    // Training
    void Train(const std::vector<std::vector<float>>& inputs,
               const std::vector<std::vector<float>>& targets,
               size_t epochs,
               float learningRate,
               size_t batchSize = 1);
    
    // Single training step
    float TrainStep(const std::vector<float>& input,
                    const std::vector<float>& target,
                    float learningRate);
    
    // Prediction
    std::vector<float> Predict(const std::vector<float>& input);
    int PredictClass(const std::vector<float>& input);
    
    // Evaluation
    float Evaluate(const std::vector<std::vector<float>>& inputs,
                   const std::vector<std::vector<float>>& targets);
    
    // Serialization
    std::string Serialize() const;
    bool Deserialize(const std::string& data);
    
    // Deep copy of layer weights/biases (layers must already be built with same shape)
    void CopyWeightsFrom(const NeuralNetwork& other);
    
    // Getters
    size_t GetLayerCount() const { return layers.size(); }
    const std::vector<std::unique_ptr<NeuralLayer>>& GetLayers() const { return layers; }
    
    // Configuration
    void SetLossFunction(const std::string& lossType);
    
private:
    std::vector<std::unique_ptr<NeuralLayer>> layers;
    std::vector<size_t> layerSizes;
    std::vector<std::string> layerActivations;
    bool built;
    std::string lossFunction;
    std::mt19937 rng;  // Build(seed)/Train shuffle 共用
    
    // Loss function and gradient
    std::function<float(const std::vector<float>&, const std::vector<float>&)> lossFn;
    std::function<std::vector<float>(const std::vector<float>&, const std::vector<float>&)> lossGradientFn;
    
    void InitializeLossFunction();
};

/**
 * Convolutional Layer (for image processing)
 */
class ConvLayer {
public:
    ConvLayer(size_t inputChannels, size_t outputChannels,
              size_t kernelSize, size_t stride = 1, size_t padding = 0);
    
    std::vector<std::vector<std::vector<float>>> Forward(
        const std::vector<std::vector<std::vector<float>>>& input);
    
    std::vector<std::vector<std::vector<float>>> Backward(
        const std::vector<std::vector<std::vector<float>>>& gradient,
        float learningRate);
    
private:
    size_t inputChannels;
    size_t outputChannels;
    size_t kernelSize;
    size_t stride;
    size_t padding;
    
    std::vector<std::vector<std::vector<std::vector<float>>>> kernels;
    std::vector<float> biases;
};

/**
 * Pooling Layer
 */
class PoolLayer {
public:
    enum class PoolType {
        Max,
        Average
    };
    
    PoolLayer(size_t poolSize, size_t stride, PoolType type = PoolType::Max);
    
    std::vector<std::vector<std::vector<float>>> Forward(
        const std::vector<std::vector<std::vector<float>>>& input);
    
    std::vector<std::vector<std::vector<float>>> Backward(
        const std::vector<std::vector<std::vector<float>>>& gradient);
    
private:
    size_t poolSize;
    size_t stride;
    PoolType type;
    std::vector<std::vector<std::pair<size_t, size_t>>> maxIndices; // For Max pooling
};

/**
 * Recurrent Neural Network Layer (LSTM)
 */
class LSTMLayer {
public:
    LSTMLayer(size_t inputSize, size_t hiddenSize);
    
    std::vector<float> Forward(const std::vector<float>& input, bool resetHidden = false);
    std::vector<float> Backward(const std::vector<float>& gradient, float learningRate);
    
    void ResetHiddenState();
    
private:
    size_t inputSize;
    size_t hiddenSize;
    
    // LSTM gates
    std::vector<float> hiddenState;
    std::vector<float> cellState;
    
    // Weights
    std::vector<std::vector<float>> Wf; // Forget gate
    std::vector<std::vector<float>> Wi; // Input gate
    std::vector<std::vector<float>> Wo; // Output gate
    std::vector<std::vector<float>> Wc; // Cell candidate
    
    std::vector<float> bf, bi, bo, bc; // Biases
};

/**
 * Attention Mechanism (for NLP)
 */
class AttentionLayer {
public:
    AttentionLayer(size_t inputSize, size_t attentionSize);
    
    std::vector<float> Forward(const std::vector<std::vector<float>>& inputs);
    std::vector<std::vector<float>> Backward(
        const std::vector<std::vector<float>>& gradient,
        float learningRate);
    
private:
    size_t inputSize;
    size_t attentionSize;
    
    std::vector<std::vector<float>> Wq; // Query
    std::vector<std::vector<float>> Wk; // Key
    std::vector<std::vector<float>> Wv; // Value
    
    std::vector<float> bq, bk, bv;
};

} // namespace AI
} // namespace Potato
