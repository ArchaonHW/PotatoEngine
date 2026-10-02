/**
 * Potato Engine Natural Language Processing Module
 * Text processing, sentiment analysis, and language understanding for AI agents
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Potato {
namespace AI {

/**
 * Token types
 */
enum class TokenType {
    Word,
    Punctuation,
    Number,
    Symbol,
    Whitespace,
    Unknown
};

/**
 * Token
 */
struct Token {
    std::string text;
    TokenType type;
    size_t position;
    
    Token() : type(TokenType::Unknown), position(0) {}
    Token(const std::string& t, TokenType ty, size_t pos) 
        : text(t), type(ty), position(pos) {}
};

/**
 * Part of Speech tags
 */
enum class POSTag {
    Noun,           // 名詞
    Verb,           // 動詞
    Adjective,      // 形容詞
    Adverb,         // 副詞
    Pronoun,        // 代詞
    Preposition,    // 介詞
    Conjunction,    // 連詞
    Determiner,     // 限定詞
    Interjection,   // 感嘆詞
    Unknown
};

/**
 * Part of Speech tagged token
 */
struct POSTaggedToken {
    Token token;
    POSTag pos;
    std::string lemma; // Base form of the word
    
    POSTaggedToken() : pos(POSTag::Unknown) {}
};

/**
 * Named Entity types
 */
enum class EntityType {
    Person,      // 人名
    Location,    // 地點
    Organization, // 組織
    Date,        // 日期
    Time,        // 時間
    Money,       // 金額
    Percentage,  // 百分比
    Email,       // 電子郵件
    URL,         // 網址
    Phone,       // 電話號碼
    Unknown
};

/**
 * Named Entity
 */
struct NamedEntity {
    std::string text;
    EntityType type;
    size_t start;
    size_t end;
    float confidence;
    
    NamedEntity() : type(EntityType::Unknown), start(0), end(0), confidence(0.0f) {}
};

/**
 * Sentiment polarity
 */
enum class SentimentPolarity {
    Positive,
    Negative,
    Neutral
};

/**
 * Sentiment analysis result
 */
struct SentimentResult {
    SentimentPolarity polarity;
    float confidence;
    float positiveScore;
    float negativeScore;
    float neutralScore;
    
    SentimentResult() 
        : polarity(SentimentPolarity::Neutral)
        , confidence(0.0f)
        , positiveScore(0.0f)
        , negativeScore(0.0f)
        , neutralScore(0.0f) {}
};

/**
 * Intent recognition result
 */
struct IntentResult {
    std::string intent;
    float confidence;
    std::vector<std::string> entities;
    
    IntentResult() : confidence(0.0f) {}
};

/**
 * Text tokenizer
 */
class Tokenizer {
public:
    Tokenizer();
    
    // Tokenize text
    std::vector<Token> Tokenize(const std::string& text);
    
    // Sentence splitting
    std::vector<std::string> SplitSentences(const std::string& text);
    
    // Word normalization
    std::string Normalize(const std::string& word) const;
    
    // Stop word detection
    bool IsStopWord(const std::string& word) const;
    
    // Add custom stop words
    void AddStopWord(const std::string& word);
    
private:
    std::unordered_map<std::string, bool> stopWords;
    TokenType ClassifyToken(const std::string& text);
};

/**
 * Part of Speech Tagger
 */
class POSTagger {
public:
    POSTagger();
    
    // Tag tokens with POS
    std::vector<POSTaggedToken> Tag(const std::vector<Token>& tokens);
    
    // Tag text directly
    std::vector<POSTaggedToken> TagText(const std::string& text);
    
    // Load model (simplified)
    bool LoadModel(const std::string& modelPath);
    
private:
    // Simplified POS tagging rules
    POSTag GuessPOSTag(const Token& token, const std::vector<Token>& context);
};

/**
 * Named Entity Recognizer
 */
class NamedEntityRecognizer {
public:
    NamedEntityRecognizer();
    
    // Extract named entities from text
    std::vector<NamedEntity> ExtractEntities(const std::string& text);
    
    // Extract from tagged tokens
    std::vector<NamedEntity> ExtractFromTokens(const std::vector<POSTaggedToken>& tokens);
    
private:
    bool IsPersonName(const std::string& text);
    bool IsEmail(const std::string& text);
    bool IsURL(const std::string& text);
    bool IsPhoneNumber(const std::string& text);
};

/**
 * Sentiment Analyzer
 */
class SentimentAnalyzer {
public:
    SentimentAnalyzer();
    
    // Analyze sentiment of text
    SentimentResult Analyze(const std::string& text);
    
    // Analyze sentiment of tokens
    SentimentResult AnalyzeTokens(const std::vector<Token>& tokens);
    
    // Load sentiment lexicon
    void LoadLexicon(const std::string& lexiconPath);
    
    // Add custom sentiment words
    void AddSentimentWord(const std::string& word, float score); // -1 to 1
    
private:
    std::unordered_map<std::string, float> sentimentLexicon;
    float ComputeSentimentScore(const std::vector<Token>& tokens);
};

/**
 * Intent Recognizer
 */
class IntentRecognizer {
public:
    IntentRecognizer();
    
    // Recognize intent from text
    IntentResult Recognize(const std::string& text);
    
    // Add training example
    void AddTrainingExample(const std::string& text, const std::string& intent);
    
    // Train model
    void Train();
    
private:
    struct TrainingExample {
        std::string text;
        std::string intent;
    };
    std::vector<TrainingExample> trainingData;
    
    std::string ClassifyIntent(const std::string& text);
};

/**
 * Text Embedding (Word vectors)
 */
class TextEmbedding {
public:
    TextEmbedding(size_t embeddingSize = 100);
    
    // Get word embedding
    std::vector<float> GetEmbedding(const std::string& word);
    
    // Get sentence embedding (average of word embeddings)
    std::vector<float> GetSentenceEmbedding(const std::string& sentence);
    
    // Similarity between two texts
    float ComputeSimilarity(const std::string& text1, const std::string& text2);
    
    // Load pre-trained embeddings
    bool LoadEmbeddings(const std::string& embeddingsPath);
    
    // Train embeddings from corpus
    void Train(const std::vector<std::string>& corpus, int epochs = 10);
    
private:
    size_t embeddingSize;
    std::unordered_map<std::string, std::vector<float>> embeddings;
    
    std::vector<float> GetRandomEmbedding();
};

/**
 * Text Generator (Simple language model)
 */
class TextGenerator {
public:
    TextGenerator();
    
    // Generate text continuation
    std::string Generate(const std::string& prompt, int maxLength = 100);
    
    // Train on corpus
    void Train(const std::vector<std::string>& corpus, int epochs = 10);
    
    // Temperature for sampling
    void SetTemperature(float temp) { temperature = temp; }
    
private:
    float temperature;
    std::unordered_map<std::string, std::vector<std::string>> ngramModel;
    int ngramSize;
    
    void BuildNGramModel(const std::vector<std::string>& corpus);
    std::string SampleNextWord(const std::string& context);
};

/**
 * Text Summarizer
 */
class TextSummarizer {
public:
    TextSummarizer();
    
    // Extractive summarization
    std::string Summarize(const std::string& text, int maxSentences = 3);
    
    // Abstractive summarization (simplified)
    std::string AbstractiveSummarize(const std::string& text, int maxLength = 100);
    
private:
    std::vector<std::string> RankSentences(const std::string& text);
    float ComputeSentenceScore(const std::string& sentence, const std::string& text);
};

/**
 * Question Answering System
 */
class QuestionAnswering {
public:
    QuestionAnswering();
    
    // Answer question from context
    std::string Answer(const std::string& question, const std::string& context);
    
    // Add knowledge base entry
    void AddKnowledge(const std::string& question, const std::string& answer);
    
private:
    std::unordered_map<std::string, std::string> knowledgeBase;
    std::string FindBestMatch(const std::string& question);
};

/**
 * Text Similarity
 */
class TextSimilarity {
public:
    // Jaccard similarity
    static float JaccardSimilarity(const std::string& text1, const std::string& text2);
    
    // Cosine similarity (requires embeddings)
    static float CosineSimilarity(const std::vector<float>& vec1, const std::vector<float>& vec2);
    
    // Levenshtein distance
    static int LevenshteinDistance(const std::string& s1, const std::string& s2);
    
    // Jaro-Winkler similarity
    static float JaroWinklerSimilarity(const std::string& s1, const std::string& s2);
};

/**
 * Text Preprocessing
 */
class TextPreprocessor {
public:
    // Clean text
    static std::string Clean(const std::string& text);
    
    // Remove HTML tags
    static std::string RemoveHTML(const std::string& text);
    
    // Remove special characters
    static std::string RemoveSpecialChars(const std::string& text);
    
    // Convert to lowercase
    static std::string ToLowercase(const std::string& text);
    
    // Remove accents
    static std::string RemoveAccents(const std::string& text);
    
    // Expand contractions
    static std::string ExpandContractions(const std::string& text);
};

/**
 * NLP Pipeline - Combines all NLP components
 */
class NLPPipeline {
public:
    NLPPipeline();
    
    // Process text through full pipeline
    struct ProcessedText {
        std::vector<Token> tokens;
        std::vector<POSTaggedToken> posTags;
        std::vector<NamedEntity> entities;
        SentimentResult sentiment;
        IntentResult intent;
        std::vector<float> embedding;
    };
    
    ProcessedText Process(const std::string& text);
    
    // Get individual components
    Tokenizer& GetTokenizer() { return tokenizer; }
    POSTagger& GetPOSTagger() { return posTagger; }
    NamedEntityRecognizer& GetNER() { return ner; }
    SentimentAnalyzer& GetSentimentAnalyzer() { return sentimentAnalyzer; }
    IntentRecognizer& GetIntentRecognizer() { return intentRecognizer; }
    TextEmbedding& GetTextEmbedding() { return textEmbedding; }
    
private:
    Tokenizer tokenizer;
    POSTagger posTagger;
    NamedEntityRecognizer ner;
    SentimentAnalyzer sentimentAnalyzer;
    IntentRecognizer intentRecognizer;
    TextEmbedding textEmbedding;
};

} // namespace AI
} // namespace Potato
