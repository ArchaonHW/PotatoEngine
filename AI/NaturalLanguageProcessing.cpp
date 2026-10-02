/**
 * Potato Engine Natural Language Processing Implementation
 */

#include "NaturalLanguageProcessing.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <regex>
#include <cmath>
#include <random>
#include <unordered_set>

namespace Potato {
namespace AI {

// ============================================================================
// Tokenizer
// ============================================================================

Tokenizer::Tokenizer() {
    // Initialize common stop words
    for (const char* word : {
        "the", "a", "an", "and", "or", "but", "is", "are", "was", "were",
        "be", "been", "being", "have", "has", "had", "do", "does", "did",
        "will", "would", "could", "should", "may", "might", "must", "shall",
        "to", "of", "in", "for", "on", "with", "at", "by", "from", "as",
        "into", "through", "during", "before", "after", "above", "below",
        "between", "under", "again", "further", "then", "once", "here",
        "there", "when", "where", "why", "how", "all", "each", "few",
        "more", "most", "other", "some", "such", "no", "nor", "not",
        "only", "own", "same", "so", "than", "too", "very", "just"
    }) {
        stopWords[word] = true;
    }
}

std::vector<Token> Tokenizer::Tokenize(const std::string& text) {
    std::vector<Token> tokens;
    size_t pos = 0;
    
    while (pos < text.length()) {
        // Skip whitespace
        while (pos < text.length() && std::isspace(text[pos])) {
            pos++;
        }
        
        if (pos >= text.length()) break;
        
        size_t start = pos;
        
        // Identify token type
        if (std::isdigit(text[pos])) {
            // Number
            while (pos < text.length() && (std::isdigit(text[pos]) || text[pos] == '.')) {
                pos++;
            }
        } else if (std::isalpha(text[pos])) {
            // Word
            while (pos < text.length() && std::isalpha(text[pos])) {
                pos++;
            }
        } else if (std::ispunct(text[pos])) {
            // Punctuation or symbol
            pos++;
        } else {
            // Unknown
            pos++;
        }
        
        std::string tokenText = text.substr(start, pos - start);
        if (!tokenText.empty()) {
            tokens.emplace_back(tokenText, ClassifyToken(tokenText), start);
        }
    }
    
    return tokens;
}

std::vector<std::string> Tokenizer::SplitSentences(const std::string& text) {
    std::vector<std::string> sentences;
    std::string current;
    
    for (char c : text) {
        current += c;
        if (c == '.' || c == '!' || c == '?') {
            sentences.push_back(current);
            current.clear();
        }
    }
    
    if (!current.empty()) {
        sentences.push_back(current);
    }
    
    return sentences;
}

std::string Tokenizer::Normalize(const std::string& word) const {
    std::string normalized = word;
    
    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), 
                   normalized.begin(), ::tolower);
    
    // Remove trailing punctuation
    while (!normalized.empty() && std::ispunct(normalized.back())) {
        normalized.pop_back();
    }
    
    return normalized;
}

bool Tokenizer::IsStopWord(const std::string& word) const {
    std::string normalized = Normalize(word);
    return stopWords.find(normalized) != stopWords.end();
}

void Tokenizer::AddStopWord(const std::string& word) {
    stopWords[Normalize(word)] = true;
}

TokenType Tokenizer::ClassifyToken(const std::string& text) {
    if (text.empty()) return TokenType::Unknown;
    
    if (std::isdigit(text[0])) {
        return TokenType::Number;
    } else if (std::isalpha(text[0])) {
        return TokenType::Word;
    } else if (std::ispunct(text[0])) {
        return TokenType::Punctuation;
    } else if (std::isspace(text[0])) {
        return TokenType::Whitespace;
    }
    
    return TokenType::Unknown;
}

// ============================================================================
// POSTagger
// ============================================================================

POSTagger::POSTagger() {
}

std::vector<POSTaggedToken> POSTagger::Tag(const std::vector<Token>& tokens) {
    std::vector<POSTaggedToken> tagged;
    
    for (size_t i = 0; i < tokens.size(); i++) {
        POSTaggedToken taggedToken;
        taggedToken.token = tokens[i];
        taggedToken.pos = GuessPOSTag(tokens[i], tokens);
        taggedToken.lemma = Tokenizer().Normalize(tokens[i].text);
        tagged.push_back(taggedToken);
    }
    
    return tagged;
}

std::vector<POSTaggedToken> POSTagger::TagText(const std::string& text) {
    Tokenizer tokenizer;
    std::vector<Token> tokens = tokenizer.Tokenize(text);
    return Tag(tokens);
}

bool POSTagger::LoadModel(const std::string& modelPath) {
    // Placeholder for loading pre-trained POS model
    return true;
}

POSTag POSTagger::GuessPOSTag(const Token& token, const std::vector<Token>& context) {
    std::string word = Tokenizer().Normalize(token.text);
    
    // Simple heuristic rules
    // Ending with -ing -> Verb
    if (word.length() > 3 && word.substr(word.length() - 3) == "ing") {
        return POSTag::Verb;
    }
    
    // Ending with -ly -> Adverb
    if (word.length() > 2 && word.substr(word.length() - 2) == "ly") {
        return POSTag::Adverb;
    }
    
    // Ending with -tion, -sion -> Noun
    if (word.length() > 4 && 
        (word.substr(word.length() - 4) == "tion" || 
         word.substr(word.length() - 4) == "sion")) {
        return POSTag::Noun;
    }
    
    // Common words
    static std::unordered_map<std::string, POSTag> commonWords = {
        {"the", POSTag::Determiner},
        {"is", POSTag::Verb},
        {"are", POSTag::Verb},
        {"was", POSTag::Verb},
        {"were", POSTag::Verb},
        {"and", POSTag::Conjunction},
        {"or", POSTag::Conjunction},
        {"but", POSTag::Conjunction},
        {"in", POSTag::Preposition},
        {"on", POSTag::Preposition},
        {"at", POSTag::Preposition},
        {"to", POSTag::Preposition},
        {"for", POSTag::Preposition},
        {"with", POSTag::Preposition},
        {"i", POSTag::Pronoun},
        {"you", POSTag::Pronoun},
        {"he", POSTag::Pronoun},
        {"she", POSTag::Pronoun},
        {"it", POSTag::Pronoun},
        {"they", POSTag::Pronoun}
    };
    
    auto it = commonWords.find(word);
    if (it != commonWords.end()) {
        return it->second;
    }
    
    // Default to Noun
    return POSTag::Noun;
}

// ============================================================================
// Named Entity Recognizer
// ============================================================================

NamedEntityRecognizer::NamedEntityRecognizer() {
}

std::vector<NamedEntity> NamedEntityRecognizer::ExtractEntities(const std::string& text) {
    Tokenizer tokenizer;
    POSTagger posTagger;
    std::vector<Token> tokens = tokenizer.Tokenize(text);
    std::vector<POSTaggedToken> tagged = posTagger.Tag(tokens);
    
    return ExtractFromTokens(tagged);
}

std::vector<NamedEntity> NamedEntityRecognizer::ExtractFromTokens(const std::vector<POSTaggedToken>& tokens) {
    std::vector<NamedEntity> entities;
    
    for (size_t i = 0; i < tokens.size(); i++) {
        const auto& token = tokens[i];
        
        NamedEntity entity;
        entity.text = token.token.text;
        entity.start = token.token.position;
        entity.end = token.token.position + token.token.text.length();
        entity.confidence = 0.8f;
        
        if (IsEmail(token.token.text)) {
            entity.type = EntityType::Email;
            entities.push_back(entity);
        } else if (IsURL(token.token.text)) {
            entity.type = EntityType::URL;
            entities.push_back(entity);
        } else if (IsPhoneNumber(token.token.text)) {
            entity.type = EntityType::Phone;
            entities.push_back(entity);
        } else if (IsPersonName(token.token.text)) {
            entity.type = EntityType::Person;
            entities.push_back(entity);
        }
    }
    
    return entities;
}

bool NamedEntityRecognizer::IsPersonName(const std::string& text) {
    // Simple heuristic: capitalized words (very basic)
    if (text.empty() || !std::isupper(text[0])) {
        return false;
    }
    
    // Check if it's a common name (would need a database in real implementation)
    static std::vector<std::string> commonNames = {
        "John", "Jane", "Mary", "David", "Michael", "Sarah", "Robert", "Lisa"
    };
    
    for (const auto& name : commonNames) {
        if (text == name) {
            return true;
        }
    }
    
    return false;
}

bool NamedEntityRecognizer::IsEmail(const std::string& text) {
    std::regex emailRegex(R"([a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})");
    return std::regex_match(text, emailRegex);
}

bool NamedEntityRecognizer::IsURL(const std::string& text) {
    std::regex urlRegex(R"(https?://[^\s]+)");
    return std::regex_match(text, urlRegex);
}

bool NamedEntityRecognizer::IsPhoneNumber(const std::string& text) {
    std::regex phoneRegex(R"(\d{3}[-.\s]?\d{3}[-.\s]?\d{4})");
    return std::regex_match(text, phoneRegex);
}

// ============================================================================
// Sentiment Analyzer
// ============================================================================

SentimentAnalyzer::SentimentAnalyzer() {
    // Initialize simple sentiment lexicon
    sentimentLexicon = {
        {"good", 0.8f},
        {"great", 0.9f},
        {"excellent", 0.95f},
        {"amazing", 0.95f},
        {"wonderful", 0.9f},
        {"fantastic", 0.9f},
        {"bad", -0.8f},
        {"terrible", -0.9f},
        {"awful", -0.9f},
        {"horrible", -0.9f},
        {"poor", -0.7f},
        {"love", 0.9f},
        {"hate", -0.9f},
        {"happy", 0.8f},
        {"sad", -0.8f},
        {"angry", -0.7f},
        {"joy", 0.8f},
        {"fear", -0.7f},
        {"disgust", -0.7f},
        {"surprise", 0.3f},
        {"like", 0.6f},
        {"dislike", -0.6f},
        {"beautiful", 0.8f},
        {"ugly", -0.7f},
        {"success", 0.8f},
        {"failure", -0.8f},
        {"win", 0.7f},
        {"lose", -0.7f},
        {"best", 0.8f},
        {"worst", -0.8f}
    };
}

SentimentResult SentimentAnalyzer::Analyze(const std::string& text) {
    Tokenizer tokenizer;
    std::vector<Token> tokens = tokenizer.Tokenize(text);
    return AnalyzeTokens(tokens);
}

SentimentResult SentimentAnalyzer::AnalyzeTokens(const std::vector<Token>& tokens) {
    SentimentResult result;
    
    float score = ComputeSentimentScore(tokens);
    
    if (score > 0.3f) {
        result.polarity = SentimentPolarity::Positive;
        result.positiveScore = score;
        result.negativeScore = 0.0f;
        result.neutralScore = 1.0f - score;
    } else if (score < -0.3f) {
        result.polarity = SentimentPolarity::Negative;
        result.positiveScore = 0.0f;
        result.negativeScore = -score;
        result.neutralScore = 1.0f + score;
    } else {
        result.polarity = SentimentPolarity::Neutral;
        result.positiveScore = 0.0f;
        result.negativeScore = 0.0f;
        result.neutralScore = 1.0f;
    }
    
    result.confidence = std::abs(score);
    
    return result;
}

void SentimentAnalyzer::LoadLexicon(const std::string& lexiconPath) {
    // Placeholder for loading sentiment lexicon from file
}

void SentimentAnalyzer::AddSentimentWord(const std::string& word, float score) {
    sentimentLexicon[word] = score;
}

float SentimentAnalyzer::ComputeSentimentScore(const std::vector<Token>& tokens) {
    Tokenizer tokenizer;
    float totalScore = 0.0f;
    int count = 0;
    
    for (const auto& token : tokens) {
        std::string word = tokenizer.Normalize(token.text);
        auto it = sentimentLexicon.find(word);
        if (it != sentimentLexicon.end()) {
            totalScore += it->second;
            count++;
        }
    }
    
    return count > 0 ? totalScore / count : 0.0f;
}

// ============================================================================
// Intent Recognizer
// ============================================================================

IntentRecognizer::IntentRecognizer() {
}

IntentResult IntentRecognizer::Recognize(const std::string& text) {
    IntentResult result;
    result.intent = ClassifyIntent(text);
    result.confidence = 0.7f; // Placeholder
    return result;
}

void IntentRecognizer::AddTrainingExample(const std::string& text, const std::string& intent) {
    TrainingExample example;
    example.text = text;
    example.intent = intent;
    trainingData.push_back(example);
}

void IntentRecognizer::Train() {
    // Placeholder for training intent classifier
}

std::string IntentRecognizer::ClassifyIntent(const std::string& text) {
    Tokenizer tokenizer;
    std::vector<Token> tokens = tokenizer.Tokenize(text);
    
    // Simple keyword-based classification
    std::string lowerText = text;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
    
    if (lowerText.find("create") != std::string::npos || 
        lowerText.find("make") != std::string::npos ||
        lowerText.find("build") != std::string::npos) {
        return "create";
    } else if (lowerText.find("delete") != std::string::npos || 
               lowerText.find("remove") != std::string::npos) {
        return "delete";
    } else if (lowerText.find("find") != std::string::npos || 
               lowerText.find("search") != std::string::npos ||
               lowerText.find("look") != std::string::npos) {
        return "search";
    } else if (lowerText.find("help") != std::string::npos) {
        return "help";
    } else if (lowerText.find("exit") != std::string::npos || 
               lowerText.find("quit") != std::string::npos) {
        return "exit";
    }
    
    return "unknown";
}

// ============================================================================
// Text Embedding
// ============================================================================

TextEmbedding::TextEmbedding(size_t embeddingSize)
    : embeddingSize(embeddingSize) {
}

std::vector<float> TextEmbedding::GetEmbedding(const std::string& word) {
    auto it = embeddings.find(word);
    if (it != embeddings.end()) {
        return it->second;
    }
    
    // Return random embedding for unknown words
    return GetRandomEmbedding();
}

std::vector<float> TextEmbedding::GetSentenceEmbedding(const std::string& sentence) {
    Tokenizer tokenizer;
    std::vector<Token> tokens = tokenizer.Tokenize(sentence);
    
    if (tokens.empty()) {
        return std::vector<float>(embeddingSize, 0.0f);
    }
    
    std::vector<float> sentenceEmbedding(embeddingSize, 0.0f);
    int count = 0;
    
    for (const auto& token : tokens) {
        std::string word = tokenizer.Normalize(token.text);
        std::vector<float> wordEmbedding = GetEmbedding(word);
        
        for (size_t i = 0; i < embeddingSize; i++) {
            sentenceEmbedding[i] += wordEmbedding[i];
        }
        count++;
    }
    
    // Average
    for (size_t i = 0; i < embeddingSize; i++) {
        sentenceEmbedding[i] /= count;
    }
    
    return sentenceEmbedding;
}

float TextEmbedding::ComputeSimilarity(const std::string& text1, const std::string& text2) {
    std::vector<float> emb1 = GetSentenceEmbedding(text1);
    std::vector<float> emb2 = GetSentenceEmbedding(text2);
    
    return TextSimilarity::CosineSimilarity(emb1, emb2);
}

bool TextEmbedding::LoadEmbeddings(const std::string& embeddingsPath) {
    // Placeholder for loading pre-trained embeddings
    return true;
}

void TextEmbedding::Train(const std::vector<std::string>& corpus, int epochs) {
    // Placeholder for training embeddings
}

std::vector<float> TextEmbedding::GetRandomEmbedding() {
    std::vector<float> embedding(embeddingSize);
    static std::mt19937 rng(std::random_device{}());
    std::normal_distribution<float> dist(0.0f, 0.1f);
    
    for (size_t i = 0; i < embeddingSize; i++) {
        embedding[i] = dist(rng);
    }
    
    return embedding;
}

// ============================================================================
// Text Generator
// ============================================================================

TextGenerator::TextGenerator()
    : temperature(1.0f)
    , ngramSize(2) {
}

std::string TextGenerator::Generate(const std::string& prompt, int maxLength) {
    std::string result = prompt;
    std::string context = prompt;
    
    for (int i = 0; i < maxLength; i++) {
        std::string nextWord = SampleNextWord(context);
        
        if (nextWord.empty()) {
            break;
        }
        
        result += " " + nextWord;
        context = nextWord;
    }
    
    return result;
}

void TextGenerator::Train(const std::vector<std::string>& corpus, int epochs) {
    BuildNGramModel(corpus);
}

void TextGenerator::BuildNGramModel(const std::vector<std::string>& corpus) {
    Tokenizer tokenizer;
    
    for (const auto& text : corpus) {
        std::vector<Token> tokens = tokenizer.Tokenize(text);
        
        for (size_t i = 0; i < tokens.size() - ngramSize; i++) {
            std::string context;
            for (size_t j = 0; j < ngramSize; j++) {
                context += tokens[i + j].text + " ";
            }
            
            std::string nextWord = tokens[i + ngramSize].text;
            ngramModel[context].push_back(nextWord);
        }
    }
}

std::string TextGenerator::SampleNextWord(const std::string& context) {
    auto it = ngramModel.find(context);
    if (it == ngramModel.end() || it->second.empty()) {
        return "";
    }
    
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, it->second.size() - 1);
    
    return it->second[dist(rng)];
}

// ============================================================================
// Text Summarizer
// ============================================================================

TextSummarizer::TextSummarizer() {
}

std::string TextSummarizer::Summarize(const std::string& text, int maxSentences) {
    std::vector<std::string> sentences = Tokenizer().SplitSentences(text);
    std::vector<std::string> ranked = RankSentences(text);
    
    std::string summary;
    for (int i = 0; i < std::min(maxSentences, static_cast<int>(ranked.size())); i++) {
        summary += ranked[i] + " ";
    }
    
    return summary;
}

std::string TextSummarizer::AbstractiveSummarize(const std::string& text, int maxLength) {
    // Simplified: return first N characters
    return text.substr(0, std::min(maxLength, static_cast<int>(text.length())));
}

std::vector<std::string> TextSummarizer::RankSentences(const std::string& text) {
    std::vector<std::string> sentences = Tokenizer().SplitSentences(text);
    
    // Sort by sentence score (simplified: by length)
    std::sort(sentences.begin(), sentences.end(), 
              [](const std::string& a, const std::string& b) {
                  return a.length() > b.length();
              });
    
    return sentences;
}

float TextSummarizer::ComputeSentenceScore(const std::string& sentence, const std::string& text) {
    // Placeholder for sentence scoring
    return static_cast<float>(sentence.length());
}

// ============================================================================
// Question Answering
// ============================================================================

QuestionAnswering::QuestionAnswering() {
}

std::string QuestionAnswering::Answer(const std::string& question, const std::string& context) {
    std::string bestMatch = FindBestMatch(question);
    
    if (!bestMatch.empty()) {
        return knowledgeBase[bestMatch];
    }
    
    // If no match found, try to extract answer from context
    Tokenizer tokenizer;
    std::vector<Token> questionTokens = tokenizer.Tokenize(question);
    std::vector<Token> contextTokens = tokenizer.Tokenize(context);
    
    // Simple keyword matching
    for (const auto& qToken : questionTokens) {
        for (const auto& cToken : contextTokens) {
            if (tokenizer.Normalize(qToken.text) == tokenizer.Normalize(cToken.text)) {
                return "Based on the context: " + context;
            }
        }
    }
    
    return "I don't have an answer for that question.";
}

void QuestionAnswering::AddKnowledge(const std::string& question, const std::string& answer) {
    std::string normalizedQuestion = question;
    std::transform(normalizedQuestion.begin(), normalizedQuestion.end(), 
                   normalizedQuestion.begin(), ::tolower);
    knowledgeBase[normalizedQuestion] = answer;
}

std::string QuestionAnswering::FindBestMatch(const std::string& question) {
    std::string normalizedQuestion = question;
    std::transform(normalizedQuestion.begin(), normalizedQuestion.end(), 
                   normalizedQuestion.begin(), ::tolower);
    
    // Exact match
    auto it = knowledgeBase.find(normalizedQuestion);
    if (it != knowledgeBase.end()) {
        return normalizedQuestion;
    }
    
    // Partial match
    for (const auto& entry : knowledgeBase) {
        if (normalizedQuestion.find(entry.first) != std::string::npos ||
            entry.first.find(normalizedQuestion) != std::string::npos) {
            return entry.first;
        }
    }
    
    return "";
}

// ============================================================================
// Text Similarity
// ============================================================================

float TextSimilarity::JaccardSimilarity(const std::string& text1, const std::string& text2) {
    Tokenizer tokenizer;
    std::vector<Token> tokens1 = tokenizer.Tokenize(text1);
    std::vector<Token> tokens2 = tokenizer.Tokenize(text2);
    
    std::unordered_set<std::string> set1, set2;
    
    for (const auto& token : tokens1) {
        set1.insert(tokenizer.Normalize(token.text));
    }
    
    for (const auto& token : tokens2) {
        set2.insert(tokenizer.Normalize(token.text));
    }
    
    int intersection = 0;
    for (const auto& word : set1) {
        if (set2.find(word) != set2.end()) {
            intersection++;
        }
    }
    
    int unionSize = set1.size() + set2.size() - intersection;
    
    return unionSize > 0 ? static_cast<float>(intersection) / unionSize : 0.0f;
}

float TextSimilarity::CosineSimilarity(const std::vector<float>& vec1, const std::vector<float>& vec2) {
    if (vec1.size() != vec2.size()) {
        return 0.0f;
    }
    
    float dotProduct = 0.0f;
    float norm1 = 0.0f;
    float norm2 = 0.0f;
    
    for (size_t i = 0; i < vec1.size(); i++) {
        dotProduct += vec1[i] * vec2[i];
        norm1 += vec1[i] * vec1[i];
        norm2 += vec2[i] * vec2[i];
    }
    
    norm1 = std::sqrt(norm1);
    norm2 = std::sqrt(norm2);
    
    if (norm1 == 0.0f || norm2 == 0.0f) {
        return 0.0f;
    }
    
    return dotProduct / (norm1 * norm2);
}

int TextSimilarity::LevenshteinDistance(const std::string& s1, const std::string& s2) {
    int m = s1.length();
    int n = s2.length();
    
    std::vector<std::vector<int>> dp(m + 1, std::vector<int>(n + 1));
    
    for (int i = 0; i <= m; i++) dp[i][0] = i;
    for (int j = 0; j <= n; j++) dp[0][j] = j;
    
    for (int i = 1; i <= m; i++) {
        for (int j = 1; j <= n; j++) {
            if (s1[i - 1] == s2[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1];
            } else {
                dp[i][j] = 1 + std::min({dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]});
            }
        }
    }
    
    return dp[m][n];
}

float TextSimilarity::JaroWinklerSimilarity(const std::string& s1, const std::string& s2) {
    int len1 = s1.length();
    int len2 = s2.length();
    
    if (len1 == 0 || len2 == 0) {
        return 0.0f;
    }
    
    int matchDistance = std::max(len1, len2) / 2 - 1;
    if (matchDistance < 0) matchDistance = 0;
    
    std::vector<bool> s1Matches(len1, false);
    std::vector<bool> s2Matches(len2, false);
    
    int matches = 0;
    int transpositions = 0;
    
    for (int i = 0; i < len1; i++) {
        int start = std::max(0, i - matchDistance);
        int end = std::min(len2 - 1, i + matchDistance);
        
        for (int j = start; j <= end; j++) {
            if (s2Matches[j] || s1[i] != s2[j]) continue;
            s1Matches[i] = true;
            s2Matches[j] = true;
            matches++;
            break;
        }
    }
    
    if (matches == 0) return 0.0f;
    
    int k = 0;
    for (int i = 0; i < len1; i++) {
        if (!s1Matches[i]) continue;
        while (!s2Matches[k]) k++;
        if (s1[i] != s2[k]) transpositions++;
        k++;
    }
    
    float jaro = (static_cast<float>(matches) / len1 + 
                 static_cast<float>(matches) / len2 + 
                 (static_cast<float>(matches) - transpositions / 2.0f) / matches) / 3.0f;
    
    // Jaro-Winkler similarity
    int prefix = 0;
    for (int i = 0; i < std::min({len1, len2, 4}); i++) {
        if (s1[i] == s2[i]) prefix++;
        else break;
    }
    
    return jaro + (0.1f * prefix * (1.0f - jaro));
}

// ============================================================================
// Text Preprocessing
// ============================================================================

std::string TextPreprocessor::Clean(const std::string& text) {
    std::string cleaned = RemoveHTML(text);
    cleaned = RemoveSpecialChars(cleaned);
    cleaned = ToLowercase(cleaned);
    return cleaned;
}

std::string TextPreprocessor::RemoveHTML(const std::string& text) {
    std::regex htmlRegex("<[^>]*>");
    return std::regex_replace(text, htmlRegex, "");
}

std::string TextPreprocessor::RemoveSpecialChars(const std::string& text) {
    std::string result;
    for (char c : text) {
        if (std::isalnum(c) || std::isspace(c)) {
            result += c;
        }
    }
    return result;
}

std::string TextPreprocessor::ToLowercase(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

std::string TextPreprocessor::RemoveAccents(const std::string& text) {
    // Simplified - would need full implementation
    return text;
}

std::string TextPreprocessor::ExpandContractions(const std::string& text) {
    // Simplified - would need full implementation
    return text;
}

// ============================================================================
// NLP Pipeline
// ============================================================================

NLPPipeline::NLPPipeline() {
}

NLPPipeline::ProcessedText NLPPipeline::Process(const std::string& text) {
    ProcessedText processed;
    
    // Tokenize
    processed.tokens = tokenizer.Tokenize(text);
    
    // POS tagging
    processed.posTags = posTagger.Tag(processed.tokens);
    
    // Named entity recognition
    processed.entities = ner.ExtractFromTokens(processed.posTags);
    
    // Sentiment analysis
    processed.sentiment = sentimentAnalyzer.AnalyzeTokens(processed.tokens);
    
    // Intent recognition
    processed.intent = intentRecognizer.Recognize(text);
    
    // Text embedding
    processed.embedding = textEmbedding.GetSentenceEmbedding(text);
    
    return processed;
}

} // namespace AI
} // namespace Potato
