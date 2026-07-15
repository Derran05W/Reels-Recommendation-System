# ReelRank Technical Design Document

## 1. Document Purpose

This document defines the architecture, scope, requirements, algorithms, interfaces, testing strategy, benchmarks, and implementation sequence for **ReelRank**, a simulated short-form video recommendation platform.

The project will demonstrate how a large corpus of short-form videos can be filtered through a multi-stage recommendation pipeline under strict latency constraints.

The system will use the existing `Derran05W/vector-db` C++20 project as its approximate nearest-neighbour retrieval layer.

The vector database currently provides:

* KD-tree exact search
* Locality-sensitive hashing
* HNSW approximate nearest-neighbour search
* SIMD-optimized distance operations
* Batch operations
* In-memory vector storage
* Persistence-related infrastructure

The existing HNSW implementation exposes configurable graph degree, construction breadth, search breadth, hierarchical graph traversal, insertion, and top-(k) similarity search.

The final system will not attempt to reproduce TikTok or Instagram Reels exactly. It will implement a measurable, scalable recommendation engine inspired by the architecture of modern short-form video platforms.

---

# 2. Project Summary

ReelRank will simulate a short-form video platform containing:

* A large set of synthetic videos
* A large population of synthetic users
* Hidden user preferences
* Observable watch, skip, like, share, and follow events
* Online user-interest updates
* Multi-stage candidate retrieval
* Detailed candidate ranking
* Exploration of uncertain content
* Feed diversity controls
* Recommendation-quality benchmarks
* Retrieval latency and throughput benchmarks

The primary system flow will be:

```text
User requests next feed
        ↓
Load user and session state
        ↓
Generate recommendation candidates
        ↓
Retrieve similar videos using vector-db/HNSW
        ↓
Add popularity, fresh-content, and exploration candidates
        ↓
Remove invalid and previously viewed items
        ↓
Score candidates with the ranking model
        ↓
Apply diversity and business-rule re-ranking
        ↓
Return ordered feed
        ↓
Simulate user interactions
        ↓
Update user state and system statistics
```

---

# 3. Core Engineering Question

The central question of the project is:

> How closely can approximate vector retrieval match exhaustive personalized search while dramatically reducing recommendation latency?

Secondary questions include:

* How quickly can the system infer a new user's interests?
* How quickly can it adapt when a user's interests change?
* How should exploration be balanced against known preferences?
* How much recommendation quality is lost when HNSW replaces brute-force search?
* How do HNSW parameters affect recall, latency, insertion cost, and memory?
* How much does second-stage ranking improve over raw vector similarity?
* How does diversity-aware re-ranking affect engagement and feed repetition?
* How does the system behave under concurrent recommendation requests?

---

# 4. Project Goals

## 4.1 Primary goals

The project must:

1. Use the existing vector database as a real recommendation-system component.
2. Support at least 100,000 synthetic videos.
3. Support at least 10,000 synthetic users.
4. Represent users and videos using numerical embeddings.
5. Retrieve candidates using HNSW.
6. Compare HNSW against exact brute-force search.
7. Rank retrieved candidates using additional video and user features.
8. Simulate user feedback.
9. Update user preferences online.
10. Measure recommendation quality.
11. Measure retrieval and serving latency.
12. Support deterministic experiments using seeded randomness.
13. Produce reproducible benchmark reports.

## 4.2 Stretch goals

The project may later support:

* One million or more videos
* 100,000 or more users
* Concurrent request simulation
* A standalone vector-search service
* REST or gRPC APIs
* Thompson-sampling exploration
* Session-aware interest drift
* Creator affinity
* New-video cold start
* New-user onboarding
* A dashboard
* Prometheus metrics
* Docker deployment
* PyTorch-trained ranking models
* ONNX inference

---

# 5. Explicit Non-Goals

The initial version will not include:

* Real video uploads
* Video playback
* Computer-vision processing
* Speech transcription
* Natural-language embedding generation
* Copyrighted datasets
* Real human user data
* Kubernetes
* Distributed consensus
* Multi-region deployment
* Production-grade authentication
* Advertising systems
* Content moderation models
* Full social networking features
* Large transformer training
* Exact reproduction of TikTok or Instagram internals

The project should simulate videos using embeddings and metadata rather than processing actual media.

---

# 6. Repository Strategy

The project should remain split into two repositories.

## 6.1 Existing repository

```text
vector-db
```

Responsibilities:

* Vector storage
* Distance metrics
* KD-tree search
* LSH search
* HNSW search
* Exact-search baseline
* Index construction
* Index tuning
* Retrieval benchmarks
* Persistence
* Search API or linked-library interface

## 6.2 New repository

```text
reel-rank
```

Responsibilities:

* User simulation
* Video simulation
* Interaction generation
* User preference estimation
* Candidate-source coordination
* Candidate ranking
* Exploration
* Feed re-ranking
* Recommendation evaluation
* Load generation
* Experiment orchestration
* Reporting
* Optional API server

## 6.3 Dependency relationship

The new project should consume `vector-db` as either:

### Initial approach

A CMake-linked library:

```cmake
target_link_libraries(reel_rank PRIVATE vector_db::vdb_core)
```

This should be used first because it minimizes integration complexity.

### Later approach

A standalone service:

```text
ReelRank Recommendation Service
             ↓
Vector Search API
             ↓
vector-db
```

The service-based version should only be implemented after the linked-library system works correctly.

---

# 7. High-Level Architecture

```text
┌─────────────────────────────────────────────────────┐
│                Experiment Controller                │
│                                                     │
│  Creates datasets, starts simulations, collects     │
│  results, compares algorithms, exports reports      │
└───────────────────────┬─────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────┐
│                  User Simulator                     │
│                                                     │
│  Hidden preferences                                │
│  Session state                                     │
│  Interaction generation                            │
│  Preference drift                                  │
└───────────────────────┬─────────────────────────────┘
                        │ feed request
                        ▼
┌─────────────────────────────────────────────────────┐
│             Recommendation Orchestrator             │
│                                                     │
│  Loads user state                                  │
│  Calls candidate generators                        │
│  Merges and deduplicates candidates                │
│  Calls ranker                                      │
│  Applies diversity rules                           │
└──────────────┬──────────────────────┬───────────────┘
               │                      │
               ▼                      ▼
┌─────────────────────────┐  ┌────────────────────────┐
│ Vector Candidate Source │  │ Other Candidate Sources│
│                         │  │                        │
│ HNSW search             │  │ Popular               │
│ Exact search baseline   │  │ Fresh                 │
│ LSH comparison          │  │ Exploration           │
└─────────────┬───────────┘  └────────────┬───────────┘
              │                           │
              └──────────────┬────────────┘
                             ▼
                 ┌────────────────────────┐
                 │ Candidate Ranker       │
                 │                        │
                 │ Similarity             │
                 │ Quality                │
                 │ Freshness              │
                 │ Creator affinity       │
                 │ Exploration bonus      │
                 │ Repetition penalty     │
                 └────────────┬───────────┘
                              ▼
                 ┌────────────────────────┐
                 │ Diversity Re-ranker    │
                 │                        │
                 │ Topic diversity        │
                 │ Creator limits         │
                 │ Duplicate removal      │
                 │ Seen-content filtering │
                 └────────────┬───────────┘
                              ▼
                         Ordered feed
                              │
                              ▼
                 ┌────────────────────────┐
                 │ Interaction Processor  │
                 │                        │
                 │ Watch time             │
                 │ Skip                   │
                 │ Like                   │
                 │ Share                  │
                 │ Follow                 │
                 └────────────┬───────────┘
                              ▼
                 ┌────────────────────────┐
                 │ User State Updater     │
                 │ Metrics Collector      │
                 └────────────────────────┘
```

---

# 8. Core Domain Model

## 8.1 Embedding

```cpp
using Embedding = std::vector<float>;
```

Initial embedding dimension:

```text
64 dimensions
```

The design must support configurable dimensions:

```text
32
64
128
256
```

All embeddings should be normalized when cosine similarity is used.

---

## 8.2 Reel

```cpp
struct Reel {
    ReelId id;
    CreatorId creatorId;

    Embedding embedding;

    float intrinsicQuality;
    float freshnessScore;
    float durationSeconds;

    TopicId primaryTopic;
    std::vector<TopicId> secondaryTopics;

    Timestamp createdAt;

    uint64_t impressionCount;
    uint64_t completionCount;
    uint64_t likeCount;
    uint64_t shareCount;
    uint64_t skipCount;

    bool active;
};
```

### Field meanings

`embedding`

Represents the semantic characteristics of the reel.

`intrinsicQuality`

A hidden or generated measure representing production quality, entertainment value, or general appeal.

`freshnessScore`

A time-decaying feature based on upload age.

`durationSeconds`

Used to simulate watch ratios and completion.

`primaryTopic` and `secondaryTopics`

Used for evaluation, diversity, and synthetic-data generation.

`interaction counters`

Used by popularity and trending candidate sources.

---

## 8.3 User

```cpp
struct User {
    UserId id;

    Embedding hiddenPreference;
    Embedding estimatedPreference;
    Embedding longTermPreference;
    Embedding sessionPreference;

    std::unordered_set<ReelId> seenReels;
    std::unordered_map<CreatorId, float> creatorAffinity;

    std::deque<InteractionEvent> recentInteractions;

    uint64_t totalInteractions;
    uint64_t currentSessionLength;
};
```

### Hidden preference

The simulation uses this to generate true user reactions.

The recommender must never access it.

### Estimated preference

The recommender's current best estimate of the user's interests.

### Long-term preference

A slower-moving representation based on historical interactions.

### Session preference

A faster-moving representation based on the current session.

### Effective recommendation vector

```text
effectivePreference =
    longTermWeight × longTermPreference
  + sessionWeight × sessionPreference
```

Initial recommended weights:

```text
longTermWeight = 0.65
sessionWeight = 0.35
```

These should be configurable.

---

## 8.4 Interaction Event

```cpp
enum class InteractionType {
    Impression,
    InstantSkip,
    PartialWatch,
    CompleteWatch,
    Rewatch,
    Like,
    Share,
    FollowCreator,
    NotInterested
};
```

```cpp
struct InteractionEvent {
    UserId userId;
    ReelId reelId;
    CreatorId creatorId;

    InteractionType type;

    float watchSeconds;
    float watchRatio;
    float reward;

    Timestamp timestamp;
    SessionId sessionId;
};
```

---

## 8.5 Candidate

```cpp
enum class CandidateSource {
    VectorHNSW,
    VectorExact,
    Popular,
    Trending,
    Fresh,
    CreatorAffinity,
    Exploration
};
```

```cpp
struct Candidate {
    ReelId reelId;

    CandidateSource source;

    float retrievalDistance;
    float retrievalSimilarity;

    float rankingScore;

    std::unordered_map<std::string, float> featureContributions;
};
```

A candidate may appear from multiple sources.

The system should preserve all source labels during deduplication.

---

## 8.6 Recommendation Request

```cpp
struct RecommendationRequest {
    UserId userId;
    SessionId sessionId;

    size_t feedSize;
    size_t candidateLimit;

    bool enableExploration;
    bool enableDiversity;

    Timestamp requestTime;
};
```

---

## 8.7 Recommendation Response

```cpp
struct RankedReel {
    ReelId reelId;
    float score;
    size_t rank;
    std::vector<CandidateSource> sources;
};
```

```cpp
struct RecommendationResponse {
    std::vector<RankedReel> reels;

    double retrievalLatencyMs;
    double rankingLatencyMs;
    double rerankingLatencyMs;
    double totalLatencyMs;

    size_t candidatesRetrieved;
    size_t candidatesRanked;
};
```

---

# 9. Synthetic Data Generation

## 9.1 Topic space

Create a configurable number of synthetic topics.

Initial value:

```text
32 topics
```

Example conceptual topics:

* Fitness
* Gaming
* Programming
* Comedy
* Travel
* Food
* Fashion
* Cars
* Sports
* Music

The actual simulation may use numeric topic identifiers rather than human-readable categories.

Each topic should have a topic-centre embedding.

```cpp
struct Topic {
    TopicId id;
    Embedding center;
};
```

Topic centres should be randomly generated and normalized.

---

## 9.2 Reel generation

Each reel should be generated from one or more topics.

Conceptually:

[
v =
\operatorname{normalize}
\left(
w_1 t_1 +
w_2 t_2 +
\epsilon
\right)
]

where:

* (t_1) is the primary topic vector.
* (t_2) is an optional secondary topic vector.
* (\epsilon) is random noise.

Generate:

* Reel embedding
* Creator
* Topic labels
* Duration
* Quality
* Creation time
* Initial popularity

Durations should follow a configurable distribution, such as:

```text
5–15 seconds
15–30 seconds
30–60 seconds
60–120 seconds
```

---

## 9.3 User generation

Each user should prefer a sparse combination of topics.

Conceptually:

[
p_u =
\operatorname{normalize}
\left(
\sum_{i=1}^{m} a_i t_i + \epsilon
\right)
]

Recommended configuration:

```text
2–5 preferred topics per user
```

Users should vary in:

* Preference concentration
* Willingness to explore
* Average session length
* Like probability
* Share probability
* Tolerance for long videos
* Preference stability

---

## 9.4 Creator generation

Creators should specialize in one or more topics.

```cpp
struct Creator {
    CreatorId id;
    Embedding styleEmbedding;
    std::vector<TopicId> topicSpecialties;
    float baseQuality;
};
```

A reel embedding may combine:

* Topic vector
* Creator style vector
* Random noise

This enables creator-affinity experiments.

---

# 10. User Behaviour Simulation

The synthetic user simulator determines the true reaction to a shown reel.

The true affinity should be based on the hidden preference, not the estimated preference.

## 10.1 Base affinity

[
a(u,v) = p_u^\top q_v
]

where:

* (p_u) is the user's hidden preference.
* (q_v) is the reel embedding.

---

## 10.2 Behaviour score

[
z =
\alpha a(u,v)
+\beta Q_v
+\gamma C_{u,c}
-\delta D_v
+\epsilon
]

where:

* (Q_v) is intrinsic quality.
* (C_{u,c}) is creator affinity.
* (D_v) is a duration-related penalty.
* (\epsilon) is random noise.

---

## 10.3 Watch probability

[
P(\text{complete}) = \sigma(z)
]

Instant-skip probability:

[
P(\text{instant skip}) = \sigma(-z + b)
]

Share and like probabilities should depend on both affinity and completed watch ratio.

---

## 10.4 Watch ratio

The watch ratio should be sampled from a distribution conditioned on affinity.

Example:

```text
Very low affinity:
    0%–10% watch ratio

Low affinity:
    10%–40%

Medium affinity:
    40%–80%

High affinity:
    80%–120%
```

Values greater than 100% represent replay behaviour.

---

## 10.5 Reward function

Initial reward:

[
R =
0.45 \times \text{watchRatioClamped}
+0.15 \times \log(1+\text{watchSeconds})
+0.15 \times \text{like}
+0.20 \times \text{share}
+0.15 \times \text{follow}
-0.35 \times \text{instantSkip}
-0.75 \times \text{notInterested}
]

Weights must be configurable.

The reward should be bounded or normalized to avoid unstable preference updates.

---

# 11. User Preference Estimation

## 11.1 Cold-start initialization

A new user begins with:

```text
estimatedPreference = globalAveragePreference
```

Alternative experiments may use:

* Random vector
* Popular-topic vector
* Onboarding-selected topics
* Demographic-free clustering priors

The hidden preference must remain inaccessible.

---

## 11.2 Online vector update

After an interaction:

[
u_{t+1}
=======

\operatorname{normalize}
\left(
(1-\eta)u_t
+
\eta r_t v_t
\right)
]

where:

* (u_t) is the estimated preference.
* (v_t) is the reel embedding.
* (r_t) is normalized reward.
* (\eta) is the learning rate.

Suggested initial values:

```text
long-term learning rate = 0.02
session learning rate = 0.15
```

Positive reward moves the user vector toward the reel.

Negative reward moves it away.

---

## 11.3 Session update

Session preference should heavily weight recent interactions.

Possible implementation:

[
s_t =
\operatorname{normalize}
\left(
\sum_{i=1}^{n}
\lambda^{n-i} r_i v_i
\right)
]

where:

```text
λ = 0.85–0.95
```

The project should initially use a fixed-size recent-interaction window.

Recommended window:

```text
20 interactions
```

---

## 11.4 Interest drift

Some experiments should modify hidden user preferences during the simulation.

Example:

```text
Interactions 0–499:
70% fitness
20% programming
10% comedy

Interactions 500+:
20% fitness
20% programming
60% travel
```

The project should measure how quickly each recommendation strategy adapts.

---

# 12. Candidate Generation

Candidate generation should use several independent sources.

Each source must implement a common interface.

```cpp
class CandidateGenerator {
public:
    virtual std::vector<Candidate> generate(
        const User& user,
        const RecommendationRequest& request
    ) = 0;

    virtual ~CandidateGenerator() = default;
};
```

---

## 12.1 HNSW vector candidate generator

Input:

```text
User effective-preference vector
```

Output:

```text
Top N nearest reel vectors
```

Initial value:

```text
N = 500
```

The existing HNSW implementation supports top-(k) search and configurable `efSearch`.

The vector database should return:

* Reel ID
* Distance
* Optional metadata

The recommendation layer should convert distance into similarity.

For normalized vectors using cosine distance:

[
similarity = 1 - distance
]

For Euclidean distance, use a monotonic transformation:

[
similarity = \frac{1}{1+d}
]

---

## 12.2 Exact vector candidate generator

This generator is used for:

* Ground truth
* Small experiments
* Recall comparison
* Quality ceiling

It should search all reels.

It must not be used in large online simulations except for benchmark sampling.

---

## 12.3 Popular candidate generator

Rank reels using a smoothed engagement score.

Example:

[
popularity =
\frac{
completionCount
+2 \times likeCount
+4 \times shareCount
}{
1+impressionCount
}
]

Use Bayesian smoothing to prevent tiny-sample videos from dominating.

---

## 12.4 Trending candidate generator

Trending should account for recent velocity rather than lifetime counts.

Example:

[
trend =
\frac{\text{recent weighted interactions}}
{1+\text{recent impressions}}
]

Use time decay:

[
w(\Delta t)=e^{-\lambda \Delta t}
]

---

## 12.5 Fresh candidate generator

Select recently uploaded reels.

Fresh candidates help solve new-video cold start.

The generator should optionally limit candidates to topics near the user's estimated interests.

---

## 12.6 Creator-affinity candidate generator

Select recent or strong-performing reels from creators the user previously enjoyed.

---

## 12.7 Exploration candidate generator

Initial implementation:

```text
Epsilon-greedy exploration
```

With probability (\epsilon):

* Add random fresh reels
* Add uncertain-topic reels
* Add underexposed reels

Recommended starting value:

```text
epsilon = 0.05
```

Later implementation:

* Thompson sampling
* LinUCB
* Uncertainty-aware candidate bonuses

---

# 13. Candidate Merge and Filtering

After candidate generation:

1. Merge candidates from all sources.
2. Deduplicate by reel ID.
3. Preserve all source labels.
4. Remove inactive reels.
5. Remove previously viewed reels where required.
6. Remove blocked creators if such a feature is added.
7. Remove invalid embeddings.
8. Cap candidate pool size.

Suggested initial candidate counts:

```text
HNSW:             500
Popular:          100
Trending:         100
Fresh:            100
Creator affinity: 100
Exploration:       50
```

After deduplication:

```text
Target candidate pool: 500–800
```

---

# 14. Candidate Ranking

The first ranking model should be deterministic and formula-based.

A neural ranker is not required initially.

## 14.1 Ranking features

The ranker should consider:

* Vector similarity
* Reel quality
* Freshness
* Popularity
* Trending score
* Creator affinity
* Reel duration preference
* Exploration bonus
* Repetition penalty
* Number of previous impressions
* Candidate-source confidence
* Session-topic similarity

---

## 14.2 Initial ranking formula

[
Score(u,v) =
w_s S(u,v)
+w_q Q(v)
+w_f F(v)
+w_p P(v)
+w_t T(v)
+w_c C(u,v)
+w_e E(u,v)
-w_r R(u,v)
]

Initial suggested weights:

```text
Similarity:        0.50
Quality:           0.10
Freshness:         0.08
Popularity:        0.07
Trending:          0.08
Creator affinity:  0.07
Exploration bonus: 0.05
Repetition penalty:0.15
```

Weights should be configuration-driven.

All features must be normalized before weighted combination.

---

## 14.3 Feature normalization

Each continuous feature should be normalized into approximately:

```text
[0, 1]
```

Possible methods:

* Min-max normalization
* Sigmoid transformation
* Percentile normalization
* Log normalization for counts

Normalization rules must be deterministic and documented.

---

## 14.4 Ranking explanation

For debugging and demonstrations, each candidate should retain feature contributions.

Example:

```json
{
  "reel_id": "reel_10292",
  "score": 0.812,
  "contributions": {
    "similarity": 0.402,
    "quality": 0.081,
    "freshness": 0.054,
    "trending": 0.063,
    "creator_affinity": 0.041,
    "repetition_penalty": -0.014
  }
}
```

This will make system behaviour easier to inspect.

---

# 15. Diversity Re-ranking

A purely score-sorted feed may contain nearly identical videos.

The re-ranker should improve feed variety.

## 15.1 Basic constraints

Initial rules:

* No repeated reel IDs
* Maximum two reels per creator in one feed
* Maximum three reels from the same primary topic in a ten-item feed
* No reel seen previously by the user
* Avoid consecutive same-topic reels where possible

---

## 15.2 Maximum Marginal Relevance

Stretch implementation:

[
MMR(v) =
\lambda relevance(v)
--------------------

(1-\lambda)
\max_{s \in selected}
similarity(v,s)
]

Recommended initial value:

```text
lambda = 0.75
```

This balances user relevance and feed variety.

---

# 16. Recommendation Strategies to Compare

All strategies must use a common interface.

```cpp
class Recommender {
public:
    virtual RecommendationResponse recommend(
        const RecommendationRequest& request
    ) = 0;

    virtual std::string name() const = 0;

    virtual ~Recommender() = default;
};
```

Implement:

## 16.1 Random recommender

Purpose:

* Quality floor
* Exploration baseline

## 16.2 Popularity recommender

Purpose:

* Non-personalized baseline

## 16.3 Exact vector recommender

Purpose:

* Personalized quality ceiling
* Ground-truth candidate retrieval

## 16.4 HNSW-only recommender

Purpose:

* Measure ANN retrieval quality
* No secondary ranking beyond similarity

## 16.5 HNSW plus ranker

Purpose:

* Main production-style model

## 16.6 HNSW plus ranker plus diversity

Purpose:

* Final initial system

## 16.7 HNSW plus ranker plus exploration

Purpose:

* Cold-start and discovery evaluation

---

# 17. Vector Database Hardening Requirements

Before relying on the HNSW backend, the vector database should be tested and potentially corrected.

The current smoke test checks insertion, exact self-match, and sorted query output across exact, LSH, and HNSW modes, but it only uses 2,000 eight-dimensional vectors and does not measure approximate recall against exact ground truth.

## 17.1 Required correctness tests

Add tests for:

* Empty index search
* First insertion
* One-element search
* Duplicate keys
* Duplicate vectors
* Self-neighbour prevention
* Dimension mismatch
* NaN values
* Very large `k`
* `k = 0`
* Multiple graph levels
* Reciprocal connections
* Maximum degree enforcement
* Deterministic seeds
* Update behaviour
* Delete behaviour, if supported
* Rebuilding from persistence
* Search after large batch insertion

---

## 17.2 HNSW review areas

The current code should be reviewed for:

### Search entry-point propagation

The insertion and search logic compute a current entry node, but `searchLayer` currently starts using the global layer entry point. The intended greedy descent may therefore not be fully propagated between levels.

The preferred interface is:

```cpp
searchLayer(
    const Vector& query,
    size_t entryPoint,
    size_t ef,
    int level
);
```

### Reciprocal-neighbour pruning

Connections are added bidirectionally, but existing nodes should be pruned back to their allowed degree after reciprocal insertion.

### Update semantics

The vector database update path reinserts updated vectors into indexes. This may create stale or duplicate index entries unless the indexes support key replacement.

The initial ReelRank version should treat reel embeddings as immutable.

### Parameter defaults

The current database constructs HNSW with relatively low construction and search breadth values.

All parameters should be externally configurable.

---

## 17.3 Required HNSW benchmarks

Benchmark combinations of:

```text
Vector count:
10,000
100,000
1,000,000

Dimensions:
32
64
128
256

M:
8
16
32

efConstruction:
50
100
200
400

efSearch:
16
32
64
128
256

k:
10
50
200
500
```

Collect:

* Recall@K
* Query p50
* Query p95
* Query p99
* Insert throughput
* Build time
* Memory usage
* Graph degree distribution
* Maximum graph level
* Distance computations per query, if feasible

---

# 18. Evaluation Metrics

## 18.1 Retrieval metrics

### Recall@K

[
Recall@K =
\frac{
|ANN_K \cap Exact_K|
}{K}
]

### Precision@K

For the synthetic ground truth:

[
Precision@K =
\frac{\text{relevant items in top K}}{K}
]

### Distance error

Compare ANN result distances with exact-neighbour distances.

---

## 18.2 Ranking metrics

### NDCG@K

Measures ordering quality.

### Mean reciprocal rank

Measures the position of the first highly relevant item.

### Average true affinity

Average hidden user-reel affinity in recommended items.

The recommender does not access hidden affinity during operation, but evaluation code may use it.

---

## 18.3 Behaviour metrics

Track:

* Average watch ratio
* Average watch seconds
* Instant-skip rate
* Completion rate
* Like rate
* Share rate
* Follow rate
* Simulated session length
* Reward per impression
* Reward per session

---

## 18.4 Diversity metrics

Track:

* Unique topics per feed
* Unique creators per feed
* Intra-list embedding similarity
* Topic concentration
* Creator concentration
* Repetition rate

---

## 18.5 Cold-start metrics

Track:

* Interactions required to reach target reward
* Interactions required to reach target NDCG
* Regret over first 10, 25, 50, and 100 impressions
* New-reel exposure
* New-reel identification accuracy

---

## 18.6 Adaptation metrics

After preference drift:

* Reward drop
* Recovery time
* Interactions until new preference detection
* Distance between estimated and hidden preference
* Cumulative regret during adaptation

---

## 18.7 Systems metrics

Track:

* Recommendation requests per second
* Interaction events per second
* p50 latency
* p95 latency
* p99 latency
* Candidate-generation latency
* Ranking latency
* Re-ranking latency
* Memory usage
* CPU utilization
* Cache-hit rate, if caching is introduced

---

# 19. Regret Evaluation

Because hidden preferences are known to the simulator, compare the recommender with an oracle.

The oracle may exhaustively score all reels using true hidden affinity.

[
Regret_t =
Reward^*_{t}
------------

Reward_{t}
]

Cumulative regret:

[
CumulativeRegret_T =
\sum_{t=1}^{T} Regret_t
]

This is especially useful for:

* New-user cold start
* Exploration evaluation
* Preference drift
* Comparing learning rates

---

# 20. Experiment Framework

Experiments must be reproducible.

```cpp
struct ExperimentConfig {
    uint64_t randomSeed;

    size_t userCount;
    size_t reelCount;
    size_t creatorCount;
    size_t topicCount;
    size_t embeddingDimensions;

    size_t interactionsPerUser;
    size_t feedSize;

    RecommendationAlgorithm algorithm;

    HNSWConfig hnsw;
    RankingConfig ranking;
    ExplorationConfig exploration;
    DiversityConfig diversity;
};
```

Each experiment should output:

* Configuration
* Seed
* Git commit
* Build mode
* Hardware description
* Result metrics
* Timing metrics
* Output file paths

---

# 21. Configuration

Use a configuration file format such as YAML, TOML, or JSON.

Example:

```yaml
simulation:
  seed: 42
  users: 10000
  reels: 100000
  creators: 5000
  topics: 32
  dimensions: 64
  interactions_per_user: 200

recommendation:
  feed_size: 10
  vector_candidates: 500
  popular_candidates: 100
  fresh_candidates: 100
  exploration_candidates: 50

hnsw:
  m: 16
  ef_construction: 200
  ef_search: 64

ranking:
  similarity_weight: 0.50
  quality_weight: 0.10
  freshness_weight: 0.08
  popularity_weight: 0.07
  trending_weight: 0.08
  creator_affinity_weight: 0.07
  exploration_weight: 0.05
  repetition_penalty: 0.15

learning:
  long_term_rate: 0.02
  session_rate: 0.15
  recent_window: 20

exploration:
  enabled: true
  epsilon: 0.05

diversity:
  enabled: true
  max_per_creator: 2
  max_per_topic: 3
  mmr_lambda: 0.75
```

---

# 22. Suggested Module Structure

```text
reel-rank/
├── CMakeLists.txt
├── README.md
├── configs/
│   ├── small.yaml
│   ├── medium.yaml
│   ├── large.yaml
│   └── benchmark.yaml
├── include/
│   ├── domain/
│   │   ├── reel.hpp
│   │   ├── user.hpp
│   │   ├── creator.hpp
│   │   ├── interaction.hpp
│   │   ├── candidate.hpp
│   │   └── recommendation.hpp
│   ├── simulation/
│   │   ├── topic_generator.hpp
│   │   ├── reel_generator.hpp
│   │   ├── user_generator.hpp
│   │   ├── behaviour_model.hpp
│   │   └── simulator.hpp
│   ├── recommendation/
│   │   ├── recommender.hpp
│   │   ├── orchestrator.hpp
│   │   ├── candidate_generator.hpp
│   │   ├── ranker.hpp
│   │   └── reranker.hpp
│   ├── candidate_sources/
│   │   ├── hnsw_source.hpp
│   │   ├── exact_source.hpp
│   │   ├── popularity_source.hpp
│   │   ├── trending_source.hpp
│   │   ├── fresh_source.hpp
│   │   └── exploration_source.hpp
│   ├── learning/
│   │   ├── user_state_updater.hpp
│   │   ├── session_model.hpp
│   │   └── reward_model.hpp
│   ├── evaluation/
│   │   ├── retrieval_metrics.hpp
│   │   ├── ranking_metrics.hpp
│   │   ├── behaviour_metrics.hpp
│   │   ├── diversity_metrics.hpp
│   │   └── experiment_report.hpp
│   └── infrastructure/
│       ├── clock.hpp
│       ├── config.hpp
│       ├── logger.hpp
│       └── random.hpp
├── src/
│   └── corresponding implementations
├── apps/
│   ├── simulate.cpp
│   ├── benchmark_retrieval.cpp
│   ├── benchmark_recommender.cpp
│   └── inspect_user.cpp
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── property/
│   └── performance/
├── scripts/
│   ├── run_experiments.py
│   ├── compare_results.py
│   └── plot_results.py
└── results/
```

---

# 23. API Boundaries

## 23.1 Vector index interface

ReelRank should depend on an abstraction rather than directly on HNSW.

```cpp
class VectorIndex {
public:
    virtual void insert(
        const ReelId& id,
        const Embedding& embedding
    ) = 0;

    virtual std::vector<VectorSearchResult> search(
        const Embedding& query,
        size_t k
    ) const = 0;

    virtual size_t size() const = 0;

    virtual ~VectorIndex() = default;
};
```

Implementations:

* `ExactVectorIndex`
* `HNSWVectorIndex`
* `LSHVectorIndex`

The HNSW implementation should wrap the existing vector database.

---

## 23.2 Ranker interface

```cpp
class Ranker {
public:
    virtual std::vector<Candidate> rank(
        const User& user,
        const std::vector<Candidate>& candidates,
        Timestamp now
    ) const = 0;

    virtual ~Ranker() = default;
};
```

---

## 23.3 Re-ranker interface

```cpp
class Reranker {
public:
    virtual std::vector<RankedReel> rerank(
        const User& user,
        const std::vector<Candidate>& rankedCandidates,
        size_t feedSize
    ) const = 0;

    virtual ~Reranker() = default;
};
```

---

## 23.4 User-state updater interface

```cpp
class UserStateUpdater {
public:
    virtual void apply(
        User& user,
        const Reel& reel,
        const InteractionEvent& interaction
    ) const = 0;

    virtual ~UserStateUpdater() = default;
};
```

---

# 24. Testing Strategy

The project should use test-driven development.

No major subsystem should be implemented without its tests being written first or alongside it.

## 24.1 Unit tests

Test:

* Vector normalization
* Dot product and distance transformations
* Reward calculation
* Watch-ratio calculation
* User-vector updates
* Session-decay calculations
* Popularity scoring
* Freshness decay
* Trending score
* Candidate deduplication
* Candidate filtering
* Ranking feature normalization
* Ranking-score calculation
* Diversity constraints
* MMR selection
* Configuration parsing

---

## 24.2 Integration tests

Test:

* Insert reels into vector database and retrieve them
* Exact and HNSW retrieval using the same embeddings
* Candidate merge across sources
* End-to-end recommendation request
* Recommendation followed by interaction
* Interaction followed by user-state update
* Multiple recommendation rounds
* Preference drift response
* Persistence recovery, where applicable

---

## 24.3 Property-based tests

Useful properties:

* Normalized embeddings remain approximately unit length.
* Ranking output is monotonically non-increasing by score before diversity re-ranking.
* No feed contains duplicate reel IDs.
* Filtered reels never reappear.
* HNSW results contain no invalid IDs.
* Increasing `efSearch` should generally not reduce recall beyond statistical tolerance.
* Candidate counts never exceed configured limits.
* Recommendation calls never modify hidden user preferences.
* Same seed and configuration produce identical simulation results.

---

## 24.4 Differential tests

Compare:

```text
HNSW search
vs
exact brute-force search
```

For randomly generated datasets:

* Compare top-(k) overlap
* Compare nearest-neighbour distance
* Verify self-match where applicable
* Verify no malformed outputs

---

## 24.5 Performance tests

Performance tests should not be run as ordinary unit tests.

Separate benchmarks should measure:

* HNSW construction time
* HNSW search latency
* Exact-search latency
* Ranking latency
* Re-ranking latency
* End-to-end recommendation latency
* Concurrent request throughput

---

## 24.6 Determinism tests

All generators must accept explicit seeds.

The following should be reproducible:

* Topic vectors
* User preferences
* Reel embeddings
* User interactions
* HNSW level generation, where deterministic testing is enabled
* Experiment outputs

---

# 25. Logging and Observability

The simulator should support:

```text
INFO
DEBUG
TRACE
WARN
ERROR
```

Log events:

* Dataset generation
* Index construction
* Experiment start and end
* Recommendation latency
* Candidate counts
* User-state changes
* Preference-drift events
* Errors
* Benchmark summaries

Avoid logging every interaction in large simulations unless trace logging is enabled.

---

# 26. Output and Reporting

Each experiment should produce:

```text
results/<experiment-id>/
├── config.yaml
├── summary.json
├── retrieval_metrics.csv
├── recommendation_metrics.csv
├── latency_metrics.csv
├── diversity_metrics.csv
├── learning_curve.csv
├── regret_curve.csv
└── metadata.json
```

Recommended graphs:

* Recall versus `efSearch`
* Latency versus `efSearch`
* Recall versus latency
* Reward versus interaction count
* Cumulative regret versus interaction count
* HNSW versus exact recommendation quality
* Diversity versus reward
* Cold-start learning curve
* Preference-drift recovery curve
* Throughput versus concurrent clients
* p99 latency versus corpus size

---

# 27. Initial Performance Targets

These targets are goals, not claims.

## Small benchmark

```text
Videos:               10,000
Users:                 1,000
Dimensions:            64
Feed size:             10
Candidates:            200
```

Expected:

```text
End-to-end p95:        under 10 ms
HNSW Recall@10:        above 90%
```

## Medium benchmark

```text
Videos:               100,000
Users:                 10,000
Dimensions:            64
Feed size:             10
Candidates:            500
```

Target:

```text
HNSW p95 retrieval:    under 10 ms
End-to-end p95:        under 25 ms
HNSW Recall@10:        above 90%
```

## Large stretch benchmark

```text
Videos:               1,000,000
Users:                 100,000
Dimensions:            128
Concurrent clients:    100–1,000
```

Targets should be established after measuring the development hardware.

All published numbers must include:

* CPU
* RAM
* Operating system
* Compiler
* Build flags
* Thread count
* Dataset size
* Embedding dimension

---

# 28. Implementation Phases

## Phase 0: Project setup

Deliverables:

* New repository
* CMake configuration
* Vector database dependency
* Unit-test framework
* Formatting and linting
* CI build
* Basic configuration loading

Exit criteria:

* Project builds on supported platforms.
* One test runs successfully.
* Vector database links successfully.

---

## Phase 1: Vector database validation

Deliverables:

* Exact-search benchmark
* HNSW recall benchmark
* Deterministic dataset generation
* HNSW correctness tests
* HNSW parameter configuration
* Documented issues and fixes

Exit criteria:

* Recall@K is measurable.
* HNSW results are compared against exact results.
* No known self-edge or entry-point errors remain.
* Search behaviour is deterministic under test configuration.

---

## Phase 2: Synthetic domain generation

Deliverables:

* Topics
* Creators
* Reels
* Users
* Hidden preferences
* Deterministic generation

Exit criteria:

* Generated embeddings are valid and normalized.
* Same seed produces identical datasets.
* User and reel distributions can be inspected.

---

## Phase 3: Behaviour simulation

Deliverables:

* Affinity model
* Watch-ratio model
* Interaction generation
* Reward model

Exit criteria:

* High-affinity reels statistically produce stronger engagement.
* Low-affinity reels statistically produce more skips.
* Behaviour remains stochastic but reproducible.

---

## Phase 4: Baseline recommenders

Deliverables:

* Random
* Popularity
* Exact vector similarity
* Evaluation harness

Exit criteria:

* Baselines run end to end.
* Exact personalization outperforms random over sufficient samples.
* Metrics are exported.

---

## Phase 5: HNSW recommendation

Deliverables:

* HNSW candidate source
* Exact-versus-HNSW comparison
* Candidate retrieval timing
* Top-(k) recall measurements

Exit criteria:

* HNSW is integrated into the feed pipeline.
* Retrieval quality and latency are reported.
* HNSW recommendation reward is compared with exact recommendation reward.

---

## Phase 6: Second-stage ranker

Deliverables:

* Feature extraction
* Feature normalization
* Weighted ranking formula
* Ranking explanations

Exit criteria:

* Ranker processes HNSW candidates.
* Ranking contribution values are inspectable.
* HNSW plus ranking is compared with HNSW similarity alone.

---

## Phase 7: Online learning

Deliverables:

* User estimated-preference update
* Long-term state
* Session state
* Recent-interaction window

Exit criteria:

* New users improve over time.
* Estimated preference moves toward hidden preference under positive feedback.
* Negative feedback moves the estimate away from disliked content.

---

## Phase 8: Exploration and cold start

Deliverables:

* Epsilon-greedy exploration
* Fresh candidate source
* Underexposed-content handling
* Cold-start experiments

Exit criteria:

* Exploration rate is configurable.
* New users receive mixed content.
* New reels receive measurable exposure.
* Exploration impact on regret is measured.

---

## Phase 9: Diversity re-ranking

Deliverables:

* Creator caps
* Topic caps
* Consecutive-topic avoidance
* Optional MMR

Exit criteria:

* Duplicate content is eliminated.
* Diversity metrics improve.
* Engagement trade-offs are measured.

---

## Phase 10: Preference drift

Deliverables:

* Scheduled hidden-preference changes
* Adaptation metrics
* Recovery plots

Exit criteria:

* Drift experiments run deterministically.
* Session-aware models adapt faster than static models.

---

## Phase 11: Load and latency testing

Deliverables:

* Concurrent simulated clients
* Requests-per-second benchmark
* p50, p95, and p99 latency
* CPU and memory metrics

Exit criteria:

* Throughput and latency are measured on documented hardware.
* Bottlenecks are profiled.
* No unsupported scalability claims are made.

---

## Phase 12: Documentation and presentation

Deliverables:

* Architecture diagram
* README
* Benchmark tables
* Result plots
* Demo script
* Resume bullets
* Known limitations
* Future-work section

Exit criteria:

* Another engineer can build and run the project.
* Results are reproducible.
* Claims are backed by data.

---

# 29. Suggested One-to-Two-Week MVP Scope

For a tightly scoped implementation, the MVP should include only:

1. Vector database validation
2. Synthetic users and reels
3. Random baseline
4. Popularity baseline
5. Exact-vector baseline
6. HNSW candidate retrieval
7. Formula-based ranking
8. Online user-vector updates
9. Recall and latency benchmarks
10. Recommendation reward comparison
11. Deterministic experiments
12. Clear README and results

Do not initially implement:

* Kafka
* Redis
* Kubernetes
* Neural rankers
* Actual video processing
* REST microservices
* Complex frontend
* Thompson sampling
* Distributed training

---

# 30. Claude Code Handoff Instructions

Claude should not begin by writing the entire project.

It should proceed in stages.

## First requested output

Claude should first produce:

1. Repository inspection summary
2. Identified HNSW correctness risks
3. Proposed module boundaries
4. Dependency graph
5. Detailed task breakdown
6. Testing plan
7. Milestone order
8. Risks and unknowns
9. Questions that materially affect architecture

Claude should not implement code during this first planning pass.

## Second requested output

After the plan is reviewed, Claude should create:

* Project skeleton
* Interfaces
* Configuration structures
* Test skeletons
* No complex algorithm implementations yet

## Implementation rule

For each subsystem:

1. Write or update tests.
2. Implement the smallest passing version.
3. Run tests.
4. Add benchmarks where relevant.
5. Refactor only after correctness is established.
6. Record assumptions.
7. Avoid unrelated architectural additions.

---

# 31. Initial Prompt for Claude Code

Use the following prompt after providing Claude access to both repositories:

```text
You are planning a new project called ReelRank.

ReelRank is a simulated short-form video recommendation platform that will use the existing vector-db repository as its candidate-retrieval layer.

The system must simulate users, reels, hidden user preferences, watch and skip events, online preference learning, HNSW candidate retrieval, second-stage ranking, exploration, diversity re-ranking, recommendation-quality metrics, and latency benchmarks.

Do not begin implementation yet.

First:

1. Inspect the entire vector-db repository.
2. Pay particular attention to its HNSW implementation, exact-search implementation, distance metrics, update semantics, concurrency model, build system, tests, and public interfaces.
3. Identify correctness risks, performance risks, missing tests, and integration constraints.
4. Compare the current HNSW behaviour with the expected HNSW algorithm at a conceptual level.
5. Propose a clean integration strategy where ReelRank initially links vector-db as a CMake dependency.
6. Produce a complete implementation plan broken into milestones and small tasks.
7. Define module boundaries and C++ interfaces.
8. Define the unit, integration, differential, property, and performance tests that should exist before each subsystem is considered complete.
9. Define measurable acceptance criteria for every milestone.
10. Explicitly list non-goals to prevent scope creep.
11. Do not introduce Kafka, Redis, Kubernetes, microservices, neural networks, or a frontend in the initial implementation.
12. Do not write production code until the planning output has been reviewed.

Use the supplied ReelRank technical design document as the source of truth.

Where the current repository conflicts with the design document, identify the conflict rather than silently changing the architecture.
```

---

# 32. Definition of MVP Completion

The MVP is complete when:

* At least 100,000 synthetic reels can be generated.
* At least 10,000 synthetic users can be generated.
* Reel embeddings are indexed using the existing HNSW system.
* Exact and HNSW retrieval can be compared.
* Recall@K is measured.
* Retrieval p50, p95, and p99 are measured.
* Random and popularity baselines exist.
* Personalized recommendations outperform random recommendations.
* HNSW recommendation quality is compared with exact retrieval.
* Users update their estimated preferences from interactions.
* New-user recommendation quality improves over time.
* Results are deterministic under fixed seeds.
* All core modules have automated tests.
* The project has reproducible benchmark instructions.
* The README clearly explains architecture and limitations.

---

# 33. Definition of Strong Portfolio Completion

The strong portfolio version additionally includes:

* Session-aware preferences
* Preference drift
* Diversity re-ranking
* Exploration
* Cold-start experiments
* One million reel benchmark
* Concurrent request benchmark
* Feature-contribution inspection
* Recall-latency trade-off graphs
* Recommendation reward comparisons
* Clean architecture diagrams
* Documented hardware and build flags
* Honest limitations and future work

---

# 34. Final Portfolio Narrative

The combined project should support the following accurate description:

> Designed and implemented a simulated short-form video recommendation platform backed by a custom C++20 vector database. Used HNSW approximate nearest-neighbour search for candidate generation, followed by feature-based ranking, online preference updates, exploration, and diversity-aware re-ranking. Benchmarked approximate retrieval against exhaustive search across recommendation quality, Recall@K, throughput, and p99 latency.

The project should emphasize two distinct accomplishments:

1. Building the low-level vector-search infrastructure.
2. Applying that infrastructure to a realistic recommendation workflow.

The objective is not to claim that ReelRank reproduces TikTok.

The objective is to demonstrate a complete understanding of:

* Embedding-based retrieval
* Approximate nearest-neighbour search
* Multi-stage recommendation
* Online learning
* Exploration versus exploitation
* Feed diversity
* Synthetic evaluation
* Performance engineering
* Test-driven system development
