/**
 * Potato Engine RAG System Implementation
 */

#include "RAGSystem.h"
#include "../Networking/HttpClient.h"
#include "../Serialization/JsonParser.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <regex>
#include <chrono>
#include <unordered_set>
#include <cstring>

namespace Potato {
namespace AI {

namespace {

// ASCII-only tolower——非 ASCII 位元組（UTF-8 多位元組）原樣保留；
// signed char 負值進 ::tolower 是 UB（MSVC CRT 會 assert）
char AsciiToLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

}  // namespace

// ============================================================================
// In-Memory Vector Database Implementation
// ============================================================================

InMemoryVectorDB::InMemoryVectorDB() {
}

InMemoryVectorDB::~InMemoryVectorDB() {
}

bool InMemoryVectorDB::AddDocument(const Document& document) {
    documents[document.id] = document;
    
    // Add chunks
    for (const auto& chunk : document.chunks) {
        AddChunk(chunk);
    }
    
    return true;
}

bool InMemoryVectorDB::AddChunk(const DocumentChunk& chunk) {
    chunks.push_back(chunk);
    return true;
}

bool InMemoryVectorDB::RemoveDocument(const std::string& documentId) {
    auto docIt = documents.find(documentId);
    if (docIt == documents.end()) {
        return false;
    }
    
    // Remove chunks
    std::string chunkIdPrefix = documentId + "_";
    chunks.erase(
        std::remove_if(chunks.begin(), chunks.end(),
            [&chunkIdPrefix](const DocumentChunk& chunk) {
                return chunk.id.find(chunkIdPrefix) == 0;
            }),
        chunks.end());
    
    documents.erase(docIt);
    return true;
}

std::vector<DocumentChunk> InMemoryVectorDB::Search(
    const std::vector<float>& queryEmbedding,
    int topK,
    float threshold) {
    
    std::vector<std::pair<float, DocumentChunk>> scoredChunks;
    
    for (auto& chunk : chunks) {
        if (chunk.embedding.empty()) {
            continue;
        }
        
        float similarity = CosineSimilarity(queryEmbedding, chunk.embedding);
        chunk.relevanceScore = similarity;
        
        if (similarity >= threshold) {
            scoredChunks.emplace_back(similarity, chunk);
        }
    }
    
    // Sort by similarity (descending)
    std::sort(scoredChunks.begin(), scoredChunks.end(),
        [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
    
    // Return top K
    std::vector<DocumentChunk> results;
    for (int i = 0; i < std::min(topK, static_cast<int>(scoredChunks.size())); i++) {
        results.push_back(scoredChunks[i].second);
    }
    
    return results;
}

std::vector<DocumentChunk> InMemoryVectorDB::SearchByKeyword(
    const std::string& keyword,
    int topK) {
    
    std::vector<std::pair<int, DocumentChunk>> scoredChunks;
    
    std::string lowerKeyword = keyword;
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), AsciiToLower);
    
    if (lowerKeyword.empty()) {
        return {};
    }
    
    for (auto& chunk : chunks) {
        std::string lowerContent = chunk.content;
        std::transform(lowerContent.begin(), lowerContent.end(), lowerContent.begin(), AsciiToLower);
        
        // Count keyword occurrences
        size_t pos = 0;
        int count = 0;
        while ((pos = lowerContent.find(lowerKeyword, pos)) != std::string::npos) {
            count++;
            pos += lowerKeyword.length();
        }
        
        if (count > 0) {
            scoredChunks.emplace_back(count, chunk);
        }
    }
    
    // Sort by count (descending)
    std::sort(scoredChunks.begin(), scoredChunks.end(),
        [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
    
    std::vector<DocumentChunk> results;
    for (int i = 0; i < std::min(topK, static_cast<int>(scoredChunks.size())); i++) {
        results.push_back(scoredChunks[i].second);
    }
    
    return results;
}

void InMemoryVectorDB::Optimize() {
    // Placeholder for optimization
}

size_t InMemoryVectorDB::GetDocumentCount() {
    return documents.size();
}

size_t InMemoryVectorDB::GetChunkCount() {
    return chunks.size();
}

std::vector<Document> InMemoryVectorDB::ListDocuments() {
    std::vector<Document> out;
    out.reserve(documents.size());
    for (const auto& kv : documents) out.push_back(kv.second);
    return out;
}

float InMemoryVectorDB::CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) {
        return 0.0f;
    }
    
    float dotProduct = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    
    for (size_t i = 0; i < a.size(); i++) {
        dotProduct += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    
    normA = std::sqrt(normA);
    normB = std::sqrt(normB);
    
    if (normA == 0.0f || normB == 0.0f) {
        return 0.0f;
    }
    
    return dotProduct / (normA * normB);
}

// ============================================================================
// Document Processor Implementation
// ============================================================================

DocumentProcessor::DocumentProcessor() {
}

DocumentProcessor::~DocumentProcessor() {
}

std::vector<DocumentChunk> DocumentProcessor::ChunkDocument(
    const Document& document,
    int chunkSize,
    int overlap) {
    
    std::vector<DocumentChunk> chunks;
    std::vector<std::string> textChunks = SplitIntoChunks(document.content, chunkSize, overlap);
    
    for (size_t i = 0; i < textChunks.size(); i++) {
        DocumentChunk chunk;
        chunk.id = document.id + "_chunk_" + std::to_string(i);
        chunk.content = textChunks[i];
        chunk.source = document.id;
        chunk.metadata = ExtractMetadata(textChunks[i]);
        chunks.push_back(chunk);
    }
    
    return chunks;
}

std::string DocumentProcessor::CleanText(const std::string& text) {
    std::string cleaned = text;
    
    // Remove extra whitespace
    cleaned = std::regex_replace(cleaned, std::regex("\\s+"), " ");
    
    // Remove special characters (optional)
    // cleaned = std::regex_replace(cleaned, std::regex("[^a-zA-Z0-9 .,!?]"), "");
    
    return cleaned;
}

std::unordered_map<std::string, std::string> DocumentProcessor::ExtractMetadata(
    const std::string& content) {
    
    std::unordered_map<std::string, std::string> metadata;
    
    // Extract basic metadata
    metadata["length"] = std::to_string(content.length());
    // isspace 只吃 unsigned char——UTF-8 負值 char 是 UB（MSVC CRT assert）
    metadata["word_count"] = std::to_string(std::count_if(content.begin(), content.end(),
        [](char c) { return std::isspace(static_cast<unsigned char>(c)) ? 1 : 0; }) + 1);
    
    return metadata;
}

std::vector<std::string> DocumentProcessor::SplitIntoChunks(
    const std::string& text,
    int chunkSize,
    int overlap) {
    
    std::vector<std::string> chunks;
    
    if (text.empty() || chunkSize <= 0) {
        return chunks;
    }
    
    size_t pos = 0;
    while (pos < text.length()) {
        size_t end = std::min(pos + chunkSize, text.length());
        
        // Try to break at word boundary
        if (end < text.length()) {
            size_t lastSpace = text.rfind(' ', end);
            if (lastSpace != std::string::npos && lastSpace > pos) {
                end = lastSpace;
            }
        }
        
        chunks.push_back(text.substr(pos, end - pos));
        if (end >= text.length()) {
            break;
        }
        pos = (end > pos + overlap) ? end - overlap : end;
    }
    
    return chunks;
}

// ============================================================================
// RAG System Implementation
// ============================================================================

RAGSystem::RAGSystem(std::unique_ptr<IVectorDatabase> db)
    : vectorDB(std::move(db))
    , llmClient(nullptr) {
    
    memset(&stats, 0, sizeof(stats));
}

RAGSystem::~RAGSystem() {
}

bool RAGSystem::Initialize(ILLMClient* client) {
    llmClient = client;
    documentProcessor = std::make_unique<DocumentProcessor>();
    return true;
}

void RAGSystem::Shutdown() {
    documentProcessor.reset();
    llmClient = nullptr;
}

void RAGSystem::SetConfig(const RAGConfig& cfg) {
    config = cfg;
}

RAGConfig RAGSystem::GetConfig() const {
    return config;
}

bool RAGSystem::AddDocument(const Document& document) {
    // Chunk the document
    std::vector<DocumentChunk> chunks = documentProcessor->ChunkDocument(
        document, config.chunkSize, config.chunkOverlap);
    
    // Generate embeddings for chunks
    if (llmClient) {
        for (auto& chunk : chunks) {
            chunk.embedding = llmClient->GenerateEmbedding(chunk.content);
        }
    }
    
    // Add to vector DB
    for (const auto& chunk : chunks) {
        vectorDB->AddChunk(chunk);
    }
    
    return vectorDB->AddDocument(document);
}

bool RAGSystem::AddDocuments(const std::vector<Document>& documents) {
    for (const auto& doc : documents) {
        if (!AddDocument(doc)) {
            return false;
        }
    }
    return true;
}

bool RAGSystem::RemoveDocument(const std::string& documentId) {
    return vectorDB->RemoveDocument(documentId);
}

std::vector<DocumentChunk> RAGSystem::Retrieve(
    const std::string& query,
    int topK) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    stats.totalQueries++;
    stats.totalRetrievals++;
    
    // Generate query embedding（無 LLM client 時為空——Keyword/BM25
    // 不需要 embedding,Hybrid 降級為純 keyword）
    std::vector<float> queryEmbedding = GenerateQueryEmbedding(query);
    const bool hasEmbedding = !queryEmbedding.empty();
    
    if (!hasEmbedding && config.strategy == RetrievalStrategy::Semantic) {
        return std::vector<DocumentChunk>();
    }
    
    // Search based on strategy
    std::vector<DocumentChunk> results;
    
    switch (config.strategy) {
        case RetrievalStrategy::Semantic:
            results = vectorDB->Search(queryEmbedding, topK, config.similarityThreshold);
            break;
            
        case RetrievalStrategy::Keyword:
            results = vectorDB->SearchByKeyword(query, topK);
            break;
            
        case RetrievalStrategy::Hybrid: {
            if (!hasEmbedding) {
                results = vectorDB->SearchByKeyword(query, topK);
                break;
            }
            // Combine semantic and keyword search
            auto semanticResults = vectorDB->Search(queryEmbedding, topK * 2, config.similarityThreshold);
            auto keywordResults = vectorDB->SearchByKeyword(query, topK * 2);
            
            // Merge and deduplicate
            std::unordered_map<std::string, DocumentChunk> merged;
            for (const auto& chunk : semanticResults) {
                merged[chunk.id] = chunk;
            }
            for (const auto& chunk : keywordResults) {
                if (merged.find(chunk.id) == merged.end()) {
                    merged[chunk.id] = chunk;
                }
            }
            
            for (const auto& pair : merged) {
                results.push_back(pair.second);
            }
            
            // Rerank if enabled
            if (config.rerank) {
                results = RerankChunks(query, results);
            }
            break;
        }
            
        case RetrievalStrategy::BM25:
            // Placeholder for BM25
            results = vectorDB->SearchByKeyword(query, topK);
            break;
    }
    
    // Limit to top K
    if (results.size() > static_cast<size_t>(topK)) {
        results.resize(topK);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    stats.averageRetrievalTime = (stats.averageRetrievalTime * (stats.totalRetrievals - 1) + duration.count() / 1000.0f) / stats.totalRetrievals;
    
    return results;
}

std::vector<DocumentChunk> RAGSystem::RetrieveWithEmbedding(
    const std::vector<float>& queryEmbedding,
    int topK) {
    
    return vectorDB->Search(queryEmbedding, topK, config.similarityThreshold);
}

std::string RAGSystem::GenerateResponse(
    const std::string& query,
    const std::vector<ChatMessage>& conversation) {
    
    // Retrieve relevant chunks
    std::vector<DocumentChunk> chunks = Retrieve(query, config.topK);
    
    // Generate response with retrieval
    return GenerateResponseWithRetrieval(query, chunks, conversation);
}

std::string RAGSystem::GenerateResponseWithRetrieval(
    const std::string& query,
    const std::vector<DocumentChunk>& retrievedChunks,
    const std::vector<ChatMessage>& conversation) {
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    stats.totalGenerations++;
    
    if (!llmClient) {
        return "LLM client not available";
    }
    
    // Build context from retrieved chunks
    std::string context = BuildContext(retrievedChunks);
    
    // Build RAG prompt
    std::string prompt = BuildRAGPrompt(query, context);
    
    // Build messages
    std::vector<ChatMessage> messages;
    messages.emplace_back(MessageRole::System, 
        "You are a helpful assistant that answers questions based on the provided context. "
        "Use the context information to provide accurate and relevant answers. "
        "If the context doesn't contain the answer, say so clearly.");
    
    // Add conversation history
    for (const auto& msg : conversation) {
        messages.push_back(msg);
    }
    
    // Add current query with context
    messages.emplace_back(MessageRole::User, prompt);
    
    // Generate response
    LLMConfig llmConfig;
    llmConfig.temperature = 0.7f;
    llmConfig.maxTokens = 1024;
    
    LLMResponse response = llmClient->ChatCompletion(messages, llmConfig);
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    stats.averageGenerationTime = (stats.averageGenerationTime * (stats.totalGenerations - 1) + duration.count() / 1000.0f) / stats.totalGenerations;
    
    return response.success ? response.content : "Failed to generate response";
}

std::vector<DocumentChunk> RAGSystem::RerankChunks(
    const std::string& query,
    const std::vector<DocumentChunk>& chunks) {
    
    // Generate query embedding
    std::vector<float> queryEmbedding = GenerateQueryEmbedding(query);
    
    if (queryEmbedding.empty()) {
        return chunks;
    }
    
    // Re-score chunks
    std::vector<std::pair<float, DocumentChunk>> reranked;
    for (auto chunk : chunks) {
        if (chunk.embedding.empty()) {
            chunk.embedding = llmClient ? llmClient->GenerateEmbedding(chunk.content) : std::vector<float>();
        }
        
        float similarity = 0.0f;
        if (!chunk.embedding.empty()) {
            // Simple cosine similarity
            float dot = 0.0f, normA = 0.0f, normB = 0.0f;
            for (size_t i = 0; i < std::min(queryEmbedding.size(), chunk.embedding.size()); i++) {
                dot += queryEmbedding[i] * chunk.embedding[i];
                normA += queryEmbedding[i] * queryEmbedding[i];
                normB += chunk.embedding[i] * chunk.embedding[i];
            }
            normA = std::sqrt(normA);
            normB = std::sqrt(normB);
            if (normA > 0 && normB > 0) {
                similarity = dot / (normA * normB);
            }
        }
        
        chunk.relevanceScore = similarity;
        reranked.emplace_back(similarity, chunk);
    }
    
    // Sort by new scores
    std::sort(reranked.begin(), reranked.end(),
        [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
    
    std::vector<DocumentChunk> results;
    for (const auto& pair : reranked) {
        results.push_back(pair.second);
    }
    
    return results;
}

RAGSystem::RAGStats RAGSystem::GetStats() const {
    return stats;
}

size_t RAGSystem::GetDocumentCount() const {
    return vectorDB->GetDocumentCount();
}

size_t RAGSystem::GetChunkCount() const {
    return vectorDB->GetChunkCount();
}

std::vector<Document> RAGSystem::ListDocuments() const {
    return vectorDB->ListDocuments();
}

void RAGSystem::ResetStats() {
    memset(&stats, 0, sizeof(stats));
}

std::vector<float> RAGSystem::GenerateQueryEmbedding(const std::string& query) {
    if (llmClient) {
        return llmClient->GenerateEmbedding(query);
    }
    return std::vector<float>();
}

std::string RAGSystem::BuildContext(const std::vector<DocumentChunk>& chunks) {
    std::stringstream context;
    context << "Context Information:\n\n";
    
    for (size_t i = 0; i < chunks.size(); i++) {
        context << "[" << (i + 1) << "] " << chunks[i].content << "\n\n";
    }
    
    return context.str();
}

std::string RAGSystem::BuildRAGPrompt(const std::string& query, const std::string& context) {
    std::stringstream prompt;
    prompt << context << "\n";
    prompt << "Question: " << query << "\n";
    prompt << "Answer: ";
    return prompt.str();
}

// ============================================================================
// Knowledge Base Implementation
// ============================================================================

KnowledgeBase::KnowledgeBase(RAGSystem* system)
    : ragSystem(system) {
}

KnowledgeBase::~KnowledgeBase() {
}

bool KnowledgeBase::AddKnowledge(const std::string& content, const std::string& source) {
    Document doc = CreateDocumentFromContent(content, source);
    return ragSystem->AddDocument(doc);
}

bool KnowledgeBase::AddKnowledgeFromFile(const std::string& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    if (ss.str().empty()) return false;
    return AddKnowledge(ss.str(), filePath);
}

bool KnowledgeBase::AddKnowledgeFromURL(const std::string& url) {
    HttpResponse r = HttpClient::Get(url);
    if (!r.Ok() || r.body.empty()) return false;
    return AddKnowledge(r.body, url);
}

std::string KnowledgeBase::Query(const std::string& question) {
    return ragSystem->GenerateResponse(question);
}

std::vector<DocumentChunk> KnowledgeBase::Search(const std::string& query, int topK) {
    return ragSystem->Retrieve(query, topK);
}

namespace {

// Document ↔ JSON 序列化（匯出格式）——自帶最小跳脫,
// 不引第三方 JSON 庫（repo 規範）
std::string KbJsonEsc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace

// 匯出格式：{"version":1,"documents":[{id,title,content,url,author,
//   source,timestamp,tags[]}]}——只存文件原文;匯入時重新分塊/embedding
bool KnowledgeBase::Export(const std::string& filePath) {
    std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\"version\":1,\"documents\":[";
    bool first = true;
    for (const auto& d : ragSystem->ListDocuments()) {
        if (!first) out << ",";
        first = false;
        out << "{\"id\":\"" << KbJsonEsc(d.id) << "\""
            << ",\"title\":\"" << KbJsonEsc(d.title) << "\""
            << ",\"content\":\"" << KbJsonEsc(d.content) << "\""
            << ",\"url\":\"" << KbJsonEsc(d.url) << "\""
            << ",\"author\":\"" << KbJsonEsc(d.author) << "\""
            << ",\"source\":\"" << KbJsonEsc(d.source) << "\""
            << ",\"timestamp\":" << d.timestamp
            << ",\"tags\":[";
        for (size_t i = 0; i < d.tags.size(); i++) {
            if (i) out << ",";
            out << "\"" << KbJsonEsc(d.tags[i]) << "\"";
        }
        out << "]}";
    }
    out << "]}";
    return out.good();
}

bool KnowledgeBase::Import(const std::string& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    JsonValue j;
    if (!JsonValue::ParseOk(ss.str(), j)) return false;
    const JsonValue& docs = j.IsArray() ? j : j["documents"];
    if (!docs.IsArray()) return false;
    bool any = false;
    for (const auto& jd : docs.AsArray()) {
        Document d;
        d.id = jd["id"].AsString();
        d.title = jd["title"].AsString();
        d.content = jd["content"].AsString();
        d.url = jd["url"].AsString();
        d.author = jd["author"].AsString();
        d.source = jd["source"].AsString();
        d.timestamp = static_cast<uint64_t>(jd["timestamp"].AsInt(0));
        for (const auto& t : jd["tags"].AsArray()) {
            d.tags.push_back(t.AsString());
        }
        if (d.content.empty()) continue;
        any |= ragSystem->AddDocument(d);
    }
    return any;
}

size_t KnowledgeBase::GetDocumentCount() const {
    return ragSystem->GetDocumentCount();
}

size_t KnowledgeBase::GetChunkCount() const {
    return ragSystem->GetChunkCount();
    return 0;
}

Document KnowledgeBase::CreateDocumentFromContent(const std::string& content, const std::string& source) {
    Document doc;
    doc.id = "doc_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    doc.title = "Knowledge Entry";
    doc.content = content;
    doc.source = source;
    doc.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    return doc;
}

// ============================================================================
// RAG Utilities Implementation
// ============================================================================

namespace RAGUtils {

Document CreateDocument(const std::string& title, const std::string& content) {
    Document doc;
    doc.id = "doc_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    doc.title = title;
    doc.content = content;
    doc.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    return doc;
}

Document CreateDocumentFromFile(const std::string& filePath) {
    // Placeholder for file reading
    Document doc;
    doc.id = "doc_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    doc.title = filePath;
    doc.content = "";
    return doc;
}

std::string NormalizeText(const std::string& text) {
    std::string normalized = text;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), AsciiToLower);
    normalized = std::regex_replace(normalized, std::regex("\\s+"), " ");
    return normalized;
}

std::vector<std::string> ExtractKeywords(const std::string& text) {
    // Placeholder for keyword extraction
    std::vector<std::string> keywords;
    return keywords;
}

float CalculateSimilarity(const std::string& text1, const std::string& text2) {
    // Simple Jaccard similarity
    std::unordered_set<char> set1(text1.begin(), text1.end());
    std::unordered_set<char> set2(text2.begin(), text2.end());
    
    size_t intersection = 0;
    for (char c : set1) {
        if (set2.count(c)) {
            intersection++;
        }
    }
    
    size_t unionSize = set1.size() + set2.size() - intersection;
    
    return unionSize > 0 ? static_cast<float>(intersection) / unionSize : 0.0f;
}

std::string FormatRetrievedChunks(const std::vector<DocumentChunk>& chunks) {
    std::stringstream ss;
    for (size_t i = 0; i < chunks.size(); i++) {
        ss << "[" << (i + 1) << "] " << chunks[i].content << "\n";
    }
    return ss.str();
}

std::string FormatCitation(const DocumentChunk& chunk) {
    std::stringstream ss;
    ss << "[" << chunk.id << "] " << chunk.source;
    return ss.str();
}

} // namespace RAGUtils

} // namespace AI
} // namespace Potato
