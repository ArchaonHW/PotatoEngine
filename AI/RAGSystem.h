/**
 * Potato Engine RAG (Retrieval-Augmented Generation) System
 * Enables AI agents to retrieve relevant information from knowledge bases
 */

#pragma once

#include "LLMIntegration.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace Potato {
namespace AI {

/**
 * Document Chunk
 */
struct DocumentChunk {
    std::string id;
    std::string content;
    std::string source;
    std::vector<float> embedding;
    std::unordered_map<std::string, std::string> metadata;
    float relevanceScore;
    
    DocumentChunk()
        : relevanceScore(0.0f) {
    }
};

/**
 * Document
 */
struct Document {
    std::string id;
    std::string title;
    std::string content;
    std::string url;
    std::string author;
    std::string source;
    uint64_t timestamp;
    std::vector<std::string> tags;
    std::vector<DocumentChunk> chunks;
    
    Document()
        : timestamp(0) {
    }
};

/**
 * Retrieval Strategy
 */
enum class RetrievalStrategy {
    Semantic,       // Semantic similarity using embeddings
    Keyword,        // Keyword matching
    Hybrid,         // Combined semantic and keyword
    BM25            // BM25 ranking
};

/**
 * RAG Config
 */
struct RAGConfig {
    int chunkSize;
    int chunkOverlap;
    int topK;              // Number of chunks to retrieve
    float similarityThreshold;
    RetrievalStrategy strategy;
    bool rerank;
    bool enableCaching;
    
    RAGConfig()
        : chunkSize(512)
        , chunkOverlap(50)
        , topK(5)
        , similarityThreshold(0.7f)
        , strategy(RetrievalStrategy::Hybrid)
        , rerank(true)
        , enableCaching(true) {
    }
};

/**
 * Vector Database Interface
 */
class IVectorDatabase {
public:
    virtual ~IVectorDatabase() = default;
    
    // Document operations
    virtual bool AddDocument(const Document& document) = 0;
    virtual bool AddChunk(const DocumentChunk& chunk) = 0;
    virtual bool RemoveDocument(const std::string& documentId) = 0;
    
    // Retrieval
    virtual std::vector<DocumentChunk> Search(
        const std::vector<float>& queryEmbedding,
        int topK,
        float threshold) = 0;
    
    virtual std::vector<DocumentChunk> SearchByKeyword(
        const std::string& keyword,
        int topK) = 0;
    
    // Maintenance
    virtual void Optimize() = 0;
    virtual size_t GetDocumentCount() = 0;
    virtual size_t GetChunkCount() = 0;
};

/**
 * In-Memory Vector Database
 */
class InMemoryVectorDB : public IVectorDatabase {
public:
    InMemoryVectorDB();
    ~InMemoryVectorDB() override;
    
    bool AddDocument(const Document& document) override;
    bool AddChunk(const DocumentChunk& chunk) override;
    bool RemoveDocument(const std::string& documentId) override;
    
    std::vector<DocumentChunk> Search(
        const std::vector<float>& queryEmbedding,
        int topK,
        float threshold) override;
    
    std::vector<DocumentChunk> SearchByKeyword(
        const std::string& keyword,
        int topK) override;
    
    void Optimize() override;
    size_t GetDocumentCount() override;
    size_t GetChunkCount() override;
    
private:
    std::vector<DocumentChunk> chunks;
    std::unordered_map<std::string, Document> documents;
    
    float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);
};

/**
 * Document Processor
 */
class DocumentProcessor {
public:
    DocumentProcessor();
    ~DocumentProcessor();
    
    // Chunking
    std::vector<DocumentChunk> ChunkDocument(
        const Document& document,
        int chunkSize,
        int overlap);
    
    // Text cleaning
    std::string CleanText(const std::string& text);
    
    // Metadata extraction
    std::unordered_map<std::string, std::string> ExtractMetadata(
        const std::string& content);
    
private:
    std::vector<std::string> SplitIntoChunks(
        const std::string& text,
        int chunkSize,
        int overlap);
};

/**
 * RAG System
 */
class RAGSystem {
public:
    RAGSystem(std::unique_ptr<IVectorDatabase> vectorDB);
    ~RAGSystem();
    
    // Initialization
    bool Initialize(ILLMClient* llmClient);
    void Shutdown();
    
    // Configuration
    void SetConfig(const RAGConfig& config);
    RAGConfig GetConfig() const;
    
    // Document management
    bool AddDocument(const Document& document);
    bool AddDocuments(const std::vector<Document>& documents);
    bool RemoveDocument(const std::string& documentId);
    
    // Retrieval
    std::vector<DocumentChunk> Retrieve(
        const std::string& query,
        int topK = 5);
    
    std::vector<DocumentChunk> RetrieveWithEmbedding(
        const std::vector<float>& queryEmbedding,
        int topK = 5);
    
    // Generation
    std::string GenerateResponse(
        const std::string& query,
        const std::vector<ChatMessage>& conversation = {});
    
    std::string GenerateResponseWithRetrieval(
        const std::string& query,
        const std::vector<DocumentChunk>& retrievedChunks,
        const std::vector<ChatMessage>& conversation = {});
    
    // Reranking
    std::vector<DocumentChunk> RerankChunks(
        const std::string& query,
        const std::vector<DocumentChunk>& chunks);
    
    // Statistics
    struct RAGStats {
        size_t totalQueries;
        size_t totalRetrievals;
        size_t totalGenerations;
        float averageRetrievalTime;
        float averageGenerationTime;
    };
    RAGStats GetStats() const;
    void ResetStats();
    
private:
    std::unique_ptr<IVectorDatabase> vectorDB;
    std::unique_ptr<DocumentProcessor> documentProcessor;
    ILLMClient* llmClient;
    RAGConfig config;
    RAGStats stats;
    
    std::vector<float> GenerateQueryEmbedding(const std::string& query);
    std::string BuildContext(const std::vector<DocumentChunk>& chunks);
    std::string BuildRAGPrompt(const std::string& query, const std::string& context);
};

/**
 * Knowledge Base
 * High-level interface for managing knowledge
 */
class KnowledgeBase {
public:
    KnowledgeBase(RAGSystem* ragSystem);
    ~KnowledgeBase();
    
    // Knowledge management
    bool AddKnowledge(const std::string& content, const std::string& source = "");
    bool AddKnowledgeFromFile(const std::string& filePath);
    bool AddKnowledgeFromURL(const std::string& url);
    
    // Query
    std::string Query(const std::string& question);
    std::vector<DocumentChunk> Search(const std::string& query, int topK = 5);
    
    // Export/Import
    bool Export(const std::string& filePath);
    bool Import(const std::string& filePath);
    
    // Statistics
    size_t GetDocumentCount() const;
    size_t GetChunkCount() const;
    
private:
    RAGSystem* ragSystem;
    
    Document CreateDocumentFromContent(const std::string& content, const std::string& source);
};

/**
 * RAG Utilities
 */
namespace RAGUtils {
    // Document creation helpers
    Document CreateDocument(const std::string& title, const std::string& content);
    Document CreateDocumentFromFile(const std::string& filePath);
    
    // Text processing
    std::string NormalizeText(const std::string& text);
    std::vector<std::string> ExtractKeywords(const std::string& text);
    
    // Similarity calculation
    float CalculateSimilarity(const std::string& text1, const std::string& text2);
    
    // Formatting
    std::string FormatRetrievedChunks(const std::vector<DocumentChunk>& chunks);
    std::string FormatCitation(const DocumentChunk& chunk);
}

} // namespace AI
} // namespace Potato
