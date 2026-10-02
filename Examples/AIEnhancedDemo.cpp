/**
 * Potato Engine AI Enhanced Demo
 * Demonstrates neural networks, reinforcement learning, and NLP capabilities
 */

#include <iostream>
#include <vector>
#include <string>
#include <iomanip>

// Include AI modules
#include "../AI/NeuralNetwork.h"
#include "../AI/ReinforcementLearning.h"
#include "../AI/NaturalLanguageProcessing.h"

using namespace Potato::AI;

void PrintSeparator(const std::string& title) {
    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << "  " << title << "\n";
    std::cout << std::string(60, '=') << "\n\n";
}

void TestNeuralNetwork() {
    PrintSeparator("Neural Network Test");
    
    // Create a simple neural network for XOR problem
    NeuralNetwork nn;
    nn.AddLayer(2);          // Input: 2 features
    nn.AddLayer(4, "relu");  // Hidden layer with 4 neurons
    nn.AddLayer(1, "sigmoid"); // Output: 1 value
    nn.Build();
    nn.SetLossFunction("mse");
    
    std::cout << "Neural Network created:\n";
    std::cout << "  Input layer: 2 neurons\n";
    std::cout << "  Hidden layer: 4 neurons (ReLU)\n";
    std::cout << "  Output layer: 1 neuron (Sigmoid)\n\n";
    
    // XOR training data
    std::vector<std::vector<float>> inputs = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f}
    };
    
    std::vector<std::vector<float>> targets = {
        {0.0f},
        {1.0f},
        {1.0f},
        {0.0f}
    };
    
    std::cout << "Training XOR problem...\n";
    nn.Train(inputs, targets, 1000, 0.1f);
    
    std::cout << "\nTesting predictions:\n";
    for (size_t i = 0; i < inputs.size(); i++) {
        std::vector<float> output = nn.Predict(inputs[i]);
        std::cout << "  Input: [" << inputs[i][0] << ", " << inputs[i][1] << "] ";
        std::cout << "Predicted: " << std::fixed << std::setprecision(3) << output[0];
        std::cout << " Target: " << targets[i][0] << "\n";
    }
    
    // Test serialization
    std::string serialized = nn.Serialize();
    std::cout << "\nNetwork serialized to " << serialized.length() << " bytes\n";
    
    NeuralNetwork nn2;
    nn2.Deserialize(serialized);
    std::cout << "Network deserialized successfully\n";
}

void TestQLearning() {
    PrintSeparator("Q-Learning Agent Test");
    
    // Create a simple 4x4 grid world
    int gridSize = 16;
    int numActions = 4; // up, down, left, right
    
    QLearningAgent agent(gridSize, numActions, 0.1f, 0.99f, 0.1f);
    
    std::cout << "Q-Learning Agent created:\n";
    std::cout << "  States: " << gridSize << "\n";
    std::cout << "  Actions: " << numActions << " (up, down, left, right)\n";
    std::cout << "  Learning rate: 0.1\n";
    std::cout << "  Discount factor: 0.99\n";
    std::cout << "  Exploration rate: 0.1\n\n";
    
    // Simulate training
    std::cout << "Simulating training episodes...\n";
    for (int episode = 0; episode < 100; episode++) {
        int state = 0; // Start at top-left
        bool done = false;
        int steps = 0;
        
        while (!done && steps < 20) {
            int action = agent.SelectAction(state);
            
            // Simulate environment
            int nextState = state;
            if (action == 0 && state >= 4) nextState -= 4; // up
            else if (action == 1 && state < 12) nextState += 4; // down
            else if (action == 2 && state % 4 != 0) nextState -= 1; // left
            else if (action == 3 && state % 4 != 3) nextState += 1; // right
            
            float reward = (nextState == 15) ? 1.0f : -0.01f;
            done = (nextState == 15);
            
            agent.Update(state, action, reward, nextState, done);
            state = nextState;
            steps++;
        }
        
        if (episode % 20 == 0) {
            std::cout << "  Episode " << episode << ": " << steps << " steps\n";
        }
    }
    
    std::cout << "\nQ-Table sample (first 5 states):\n";
    for (int s = 0; s < 5; s++) {
        std::cout << "  State " << s << ": ";
        for (int a = 0; a < numActions; a++) {
            std::cout << std::fixed << std::setprecision(2) << agent.GetQValue(s, a) << " ";
        }
        std::cout << "\n";
    }
}

void TestMultiArmedBandit() {
    PrintSeparator("Multi-Armed Bandit Test");
    
    int numArms = 5;
    MultiArmedBandit bandit(numArms, 0.1f);
    
    std::cout << "Multi-Armed Bandit created:\n";
    std::cout << "  Number of arms: " << numArms << "\n";
    std::cout << "  Exploration rate: 0.1\n\n";
    
    // Simulate pulls with different reward probabilities
    std::vector<float> trueRewards = {0.3f, 0.7f, 0.5f, 0.9f, 0.2f};
    
    std::cout << "Simulating 1000 pulls...\n";
    for (int i = 0; i < 1000; i++) {
        int arm = bandit.SelectArm();
        
        // Simulate reward based on true probability
        float reward = (std::rand() % 100) / 100.0f < trueRewards[arm] ? 1.0f : 0.0f;
        bandit.Update(arm, reward);
    }
    
    std::cout << "\nEstimated values vs true probabilities:\n";
    const auto& values = bandit.GetValues();
    const auto& counts = bandit.GetCounts();
    
    for (int i = 0; i < numArms; i++) {
        std::cout << "  Arm " << i << ": ";
        std::cout << "Estimated=" << std::fixed << std::setprecision(2) << values[i];
        std::cout << " True=" << trueRewards[i];
        std::cout << " Pulls=" << counts[i] << "\n";
    }
}

void TestNLP() {
    PrintSeparator("Natural Language Processing Test");
    
    NLPPipeline pipeline;
    
    std::string text = "I love this amazing game! The graphics are beautiful and the gameplay is fantastic.";
    
    std::cout << "Analyzing text: \"" << text << "\"\n\n";
    
    NLPPipeline::ProcessedText processed = pipeline.Process(text);
    
    std::cout << "Tokens:\n";
    for (const auto& token : processed.tokens) {
        std::cout << "  " << token.text << " (" << static_cast<int>(token.type) << ")\n";
    }
    
    std::cout << "\nSentiment Analysis:\n";
    std::cout << "  Polarity: ";
    switch (processed.sentiment.polarity) {
        case SentimentPolarity::Positive: std::cout << "Positive"; break;
        case SentimentPolarity::Negative: std::cout << "Negative"; break;
        case SentimentPolarity::Neutral: std::cout << "Neutral"; break;
    }
    std::cout << "\n";
    std::cout << "  Confidence: " << std::fixed << std::setprecision(2) << processed.sentiment.confidence << "\n";
    std::cout << "  Positive score: " << processed.sentiment.positiveScore << "\n";
    std::cout << "  Negative score: " << processed.sentiment.negativeScore << "\n";
    
    std::cout << "\nIntent Recognition:\n";
    std::cout << "  Intent: " << processed.intent.intent << "\n";
    std::cout << "  Confidence: " << processed.intent.confidence << "\n";
    
    std::cout << "\nNamed Entities:\n";
    for (const auto& entity : processed.entities) {
        std::cout << "  " << entity.text << " (Type: " << static_cast<int>(entity.type) << ")\n";
    }
    
    // Test text similarity
    std::string text1 = "The game is fun and exciting";
    std::string text2 = "The game is enjoyable and thrilling";
    
    float similarity = TextSimilarity::JaccardSimilarity(text1, text2);
    std::cout << "\nText Similarity:\n";
    std::cout << "  Text 1: \"" << text1 << "\"\n";
    std::cout << "  Text 2: \"" << text2 << "\"\n";
    std::cout << "  Jaccard Similarity: " << std::fixed << std::setprecision(3) << similarity << "\n";
    
    // Test question answering
    QuestionAnswering qa;
    qa.AddKnowledge("What is the game about?", "The game is about adventure and exploration.");
    qa.AddKnowledge("How do I play?", "Use arrow keys to move and space to jump.");
    
    std::string answer = qa.Answer("What is the game?", "");
    std::cout << "\nQuestion Answering:\n";
    std::cout << "  Question: What is the game?\n";
    std::cout << "  Answer: " << answer << "\n";
}

void TestTextEmbedding() {
    PrintSeparator("Text Embedding Test");
    
    TextEmbedding embedding(50);
    
    std::string word1 = "game";
    std::string word2 = "play";
    std::string word3 = "computer";
    
    std::cout << "Computing embeddings for words:\n";
    std::vector<float> emb1 = embedding.GetEmbedding(word1);
    std::vector<float> emb2 = embedding.GetEmbedding(word2);
    std::vector<float> emb3 = embedding.GetEmbedding(word3);
    
    std::cout << "  " << word1 << ": " << emb1.size() << " dimensions\n";
    std::cout << "  " << word2 << ": " << emb2.size() << " dimensions\n";
    std::cout << "  " << word3 << ": " << emb3.size() << " dimensions\n";
    
    float sim12 = TextSimilarity::CosineSimilarity(emb1, emb2);
    float sim13 = TextSimilarity::CosineSimilarity(emb1, emb3);
    float sim23 = TextSimilarity::CosineSimilarity(emb2, emb3);
    
    std::cout << "\nCosine Similarities:\n";
    std::cout << "  " << word1 << " - " << word2 << ": " << std::fixed << std::setprecision(3) << sim12 << "\n";
    std::cout << "  " << word1 << " - " << word3 << ": " << std::fixed << std::setprecision(3) << sim13 << "\n";
    std::cout << "  " << word2 << " - " << word3 << ": " << std::fixed << std::setprecision(3) << sim23 << "\n";
}

void TestExplorationStrategies() {
    PrintSeparator("Exploration Strategies Test");
    
    std::vector<Action> actions = {
        Action(0, "action1"),
        Action(1, "action2"),
        Action(2, "action3"),
        Action(3, "action4")
    };
    
    std::vector<float> values = {0.5f, 0.8f, 0.3f, 0.6f};
    std::vector<int> counts = {10, 20, 5, 15};
    
    std::cout << "Action values: ";
    for (float v : values) std::cout << v << " ";
    std::cout << "\n";
    
    std::cout << "Action counts: ";
    for (int c : counts) std::cout << c << " ";
    std::cout << "\n\n";
    
    Action epsilonGreedy = Exploration::EpsilonGreedy(actions, values, 0.2f);
    std::cout << "Epsilon-Greedy (epsilon=0.2): " << epsilonGreedy.name << "\n";
    
    Action ucb = Exploration::UCB(actions, values, counts, 1.0f, 50);
    std::cout << "UCB (c=1.0): " << ucb.name << "\n";
    
    Action boltzmann = Exploration::Boltzmann(actions, values, 1.0f);
    std::cout << "Boltzmann (temperature=1.0): " << boltzmann.name << "\n";
}

int main() {
    std::cout << "==============================================\n";
    std::cout << "  Potato Engine AI Enhanced Demo\n";
    std::cout << "  Neural Networks, RL, and NLP\n";
    std::cout << "==============================================\n\n";
    
    try {
        TestNeuralNetwork();
        TestQLearning();
        TestMultiArmedBandit();
        TestNLP();
        TestTextEmbedding();
        TestExplorationStrategies();
        
        PrintSeparator("All Tests Completed Successfully");
        std::cout << "AI Enhanced capabilities demonstrated:\n";
        std::cout << "  - Neural Networks (XOR problem)\n";
        std::cout << "  - Q-Learning (Grid world)\n";
        std::cout << "  - Multi-Armed Bandit\n";
        std::cout << "  - Natural Language Processing\n";
        std::cout << "  - Text Embeddings\n";
        std::cout << "  - Exploration Strategies\n\n";
        
        std::cout << "Press Enter to exit...\n";
        std::cin.get();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
