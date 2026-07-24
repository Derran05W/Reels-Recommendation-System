# ReelRank Simulator Realism V2 — Technical Design Document

> **Provenance.** Source of truth for the ReelRank Realism V2 upgrade (Phases 13–24). Companion to
> the V1 TDD (`REELS-SIMULATION.md`), which remains authoritative for the already-built system;
> where the two conflict on *future* behaviour (e.g. V1 §10's predetermined session lengths vs.
> V2 §4.8's probabilistic exit), this document wins for gated-on V2 paths. Phase mapping lives in
> `plan/04-PHASES-SATISFACTION-SESSIONS.md`, `plan/05-PHASES-EVENTS-LONGTERM.md`,
> `plan/06-PHASES-LEARNED-RANKING.md`; binding decisions in `plan/00-DESIGN-DECISIONS-V2.md`.
> The equation blocks in §4.7–§4.9 and §4.15 were restored from a markdown-mangled original;
> no semantic edits were made. §4.5 does not exist in the source numbering (kept as-is so
> section references stay stable).

## 1. Purpose

Extend ReelRank from a controlled recommendation-system evaluation harness into a more realistic
closed-loop short-video platform simulator.

The upgraded simulator must model:

* Satisfaction as distinct from engagement
* Multiple reasons a user may enjoy, tolerate or engage with a reel
* Misleading engagement such as rage-watching and clickbait
* Session fatigue, repetition and creator overexposure
* Independent user timelines and return behaviour
* Feed refresh and adaptation during active sessions
* Long-term consequences of ranking decisions

The goal is not to claim reproduction of Instagram's private implementation. The goal is to
recreate the core uncertainty and optimization difficulty of a large short-video recommender.

---

## 2. Existing System

ReelRank V1 currently provides:

* Synthetic users with hidden preference embeddings
* Long-term and session-level estimated preferences
* Multi-topic reels with creator identity, duration, quality and freshness
* HNSW and exact vector retrieval
* Multiple candidate sources
* Feature-weighted ranking
* Diversity reranking
* Watch, skip, completion, rewatch, like, share, follow and not-interested events
* Online preference updates after every impression
* Scheduled hidden-preference drift
* New-user and new-reel injection
* Deterministic experiments and oracle evaluation
* Logical time advancement based on watch duration

### Main V1 limitations

1. A single semantic embedding explains most satisfaction.
2. Observable engagement is treated as a reliable proxy for enjoyment.
3. Users are processed sequentially in round-robin order.
4. Feeds are generated as fixed batches and are not refreshed during consumption.
5. Session exit is largely predetermined rather than caused by feed quality.
6. Repetition penalties exist in ranking, but user fatigue is not a rich hidden behavioural process.
7. Reel characteristics such as music, humour, usefulness and emotional reaction are not independently represented.
8. Hidden preferences mostly change through scheduled drift rather than exposure.
9. The ranker is manually weighted and does not learn outcome probabilities from logs.
10. There is no explicit long-term satisfaction, regret or return-probability objective.

---

# 3. Design Principles

## 3.1 Hidden truth remains isolated

All genuine user satisfaction, mood, fatigue and latent traits remain simulator-owned.

The recommender may observe only:

* Reel features available at serving time
* Historical interaction events
* Session context
* Population aggregates
* Explicit feedback supplied by the user

It must never read hidden satisfaction or hidden preference state directly.

## 3.2 Engagement is evidence, not truth

Watch time, likes and shares must be noisy observations.

Examples:

* Ragebait can create long watch time and comments while reducing satisfaction.
* A useful tutorial may create high satisfaction without a like.
* A familiar song may produce a rewatch even when the topic is irrelevant.
* A creator's popularity may cause a like through social conformity.
* A reel may be completed only because it is short.
* A user may stop scrolling after several acceptable videos because they are simply tired.

## 3.3 Every added mechanism must support an experiment

A new field or model is justified only when it enables a measurable comparison.

The project should prioritize experimental depth over an unstructured collection of features.

---

# 4. Tiered Implementation Plan

## Tier 1 — Richer Content and Hidden Satisfaction

### Objective

Break the direct equivalence between semantic similarity, engagement and satisfaction.

### 4.1 Reel factor model

Replace the idea that one embedding completely describes a reel with several feature groups.

```cpp
struct ReelAttributes {
    Embedding semanticEmbedding;
    Embedding visualStyleEmbedding;
    Embedding musicEmbedding;
    Embedding emotionalToneEmbedding;

    float usefulness;
    float humour;
    float novelty;
    float productionQuality;
    float controversy;
    float clickbaitStrength;
    float informationDensity;
    float emotionalIntensity;

    LanguageId language;
    float durationSeconds;
};
```

The ANN index should initially continue indexing only `semanticEmbedding` or a learned retrieval
embedding.

The remaining fields become ranker and behaviour-model features.

### 4.2 Hidden user preference channels

```cpp
struct HiddenUserState {
    Embedding topicPreference;
    Embedding visualPreference;
    Embedding musicPreference;
    Embedding emotionalPreference;

    float usefulnessPreference;
    float humourPreference;
    float controversyTolerance;
    float noveltySeeking;
    float clickbaitSusceptibility;
    float informationTolerance;
};
```

A user may therefore enjoy a reel for different reasons:

* Topic match
* Music match
* Visual style
* Useful information
* Humour
* Emotional resonance
* Creator attachment
* Novelty

### 4.3 Distinct latent outputs

For every impression, calculate:

```cpp
struct LatentReaction {
    float immediateSatisfaction;   // enjoyment during viewing
    float informationalValue;      // perceived usefulness
    float emotionalValue;          // amusement, inspiration, connection
    float regret;                  // wishes they had skipped
    float desireForSimilarContent;
    float fatigueDelta;
};
```

These values are hidden from the recommender.

Observable behaviour is sampled conditionally from them but is not identical to them.

### 4.4 Behaviour archetypes

Add configurable reel archetypes:

* **Genuinely satisfying:** high satisfaction and strong positive engagement
* **Useful:** high informational value, moderate watch, low like probability
* **Ragebait:** high watch/comment probability, negative satisfaction
* **Clickbait:** strong opening retention followed by early abandonment and regret
* **Comfort content:** moderate engagement but positive return probability
* **Highly polished but irrelevant:** catches attention without lasting value
* **Niche treasure:** highly satisfying to a small user cohort
* **Background music reel:** music-driven completion despite weak topic match

Archetypes should be probabilistic combinations of features, not hard-coded labels visible to the
ranker.

### Acceptance criteria

* Correlation between watch time and satisfaction is positive but imperfect.
* At least one experimental population produces high-engagement/low-satisfaction content.
* A watch-time ranker must outperform random on watch time while potentially underperforming on
  long-term satisfaction.
* Hidden latent values remain inaccessible to recommendation code.

### Core experiment

Compare:

1. Semantic-similarity ranker
2. Engagement-optimized ranker
3. Satisfaction-proxy ranker
4. Oracle satisfaction ranker, evaluation only

Report:

* Watch time
* Likes/shares
* Hidden satisfaction
* Regret
* Session exits
* Return rate

---

## Tier 2 — Session Dynamics, Fatigue and Exit Behaviour

### Objective

Make a session's duration an outcome of recommendation quality rather than primarily a sampled
fixed length.

### 4.6 Dynamic user state

```cpp
struct HiddenSessionState {
    float currentSatisfaction;
    float accumulatedRegret;
    float generalFatigue;
    float noveltyNeed;
    float boredom;
    float remainingAttention;

    std::unordered_map<TopicId, float> topicFatigue;
    std::unordered_map<CreatorId, float> creatorFatigue;
};
```

### 4.7 Repetition and fatigue

Each consumed reel updates:

* Topic fatigue
* Creator fatigue
* Format fatigue
* Music repetition
* Emotional-intensity fatigue
* General scrolling fatigue

Example:

```text
effective utility = base utility
                  − α · topic fatigue
                  − β · creator fatigue
                  + γ · novelty match
```

Fatigue decays while the user is away from the application.

### 4.8 Probabilistic session exit

After every impression, calculate:

```text
P(exit) = σ( b0
           + b1 · fatigue
           + b2 · recent regret
           + b3 · consecutive poor reels
           − b4 · recent satisfaction
           + b5 · external interruption )
```

Not every exit is a ranking failure.

Classify exits as:

* **Failure exit:** early exit following poor recommendations
* **Satisfied exit:** long productive session ending naturally
* **Fatigue exit:** acceptable recommendations but depleted attention
* **External exit:** independent interruption
* **Regret exit:** departure after ragebait/clickbait or repeated bad content

### 4.9 Session outcome metric

Do not reward unlimited time-on-app.

Define session utility:

```text
U_s = Σ_t satisfaction_t
    − λ1 · Σ_t regret_t
    − λ2 · harmful fatigue
    − λ3 · early failure exit
```

Measure:

* Time before exit
* Satisfaction per minute
* Regret per minute
* Early failure-exit rate
* Natural session-completion rate
* Probability of returning
* Next-session starting satisfaction

A four-hour session should not automatically be considered better than a focused twenty-minute
session.

### 4.10 Personalized diversity

Replace universal repetition penalties with user-dependent tolerance.

Some users prefer:

* Highly concentrated topic sessions
* High novelty
* Repeated favourite creators
* Mixed-topic discovery
* Music-heavy sessions
* Utility-focused sessions

The ranker estimates these tolerances from behaviour; the simulator owns the truth.

### Acceptance criteria

* Repeated same-topic content should affect users differently.
* Poor feeds should measurably increase early failure exits.
* Good long sessions should be distinguishable from addictive high-regret sessions.
* Time away from the app must reduce fatigue.
* Re-entering users must preserve long-term preferences but receive a fresh session state.

### Core experiment

Compare fixed diversity against personalized diversity under:

* Focused users
* Novelty-seeking users
* Creator-loyal users
* Easily fatigued users

---

## Tier 3 — Independent User Clocks and Event-Driven Simulation

### Objective

Replace round-robin processing with realistic asynchronous activity.

### 4.11 Event queue

Use a priority queue ordered by simulated timestamp.

```cpp
enum class EventType {
    OpenApp,
    RequestFeed,
    StartReel,
    FinishReel,
    Interaction,
    ExitApp,
    ReturnToApp,
    PreferenceDrift,
    ReelPublished
};

struct SimulationEvent {
    Timestamp time;
    uint64_t deterministicTieBreaker;
    UserId userId;
    EventType type;
};
```

Each user owns:

```cpp
struct UserTimeline {
    Timestamp nextEventAt;
    bool online;
    SessionId activeSession;
    std::deque<RankedReel> prefetchedFeed;
};
```

### 4.12 Independent behaviour

Users may:

* Open the app at different times
* Consume reels concurrently in simulated time
* Exit after any reel
* Return minutes or hours later
* Have multiple sessions per day
* Experience different time-of-day intents
* Request feed replenishment at different times

The simulator processes events sequentially for determinism, but event timestamps represent
concurrency.

### 4.13 Feed prefetch and refresh

Add configurable serving strategies:

* Generate one reel at a time
* Prefetch 3 reels
* Prefetch 10 reels
* Refill when remaining inventory reaches a threshold
* Invalidate remaining feed after major session-intent changes
* Preserve already-downloaded reels even when the preference estimate changes

This enables a freshness-versus-cost experiment.

### 4.14 Shared global trends

Trending and popularity updates occur at each event timestamp.

Two users requesting at the same simulated time should observe the same prior global state. Use
deterministic tie rules and optionally process equal-timestamp requests from a snapshot.

### Acceptance criteria

* Changing user iteration order must not materially change results.
* Users must have independent app-entry and return schedules.
* Feed requests should occur based on consumption and refill thresholds.
* Simultaneous request semantics must be explicitly defined and tested.
* Same seed and configuration must remain reproducible.

### Core experiment

Compare batch sizes 1, 3, 10 and 20 under abrupt preference drift.

Report:

* Adaptation delay
* Satisfaction lost before refresh
* Number of feed requests
* Ranking computation count
* Stale-feed impression rate

---

## Tier 4 — Exposure-Driven Preferences and Long-Term Outcomes

### Objective

Model the fact that recommendations do not merely discover preferences; they can influence them.

### 4.15 Preference reinforcement

Repeated satisfying exposure may strengthen interest:

```text
p_{t+1} = normalize( (1 − η) · p_t + η · s_t · v_t )
```

where `s_t` is hidden satisfaction rather than observed engagement.

### 4.16 Saturation and aversion

Repeated exposure can also cause:

* Topic exhaustion
* Creator burnout
* Reduced novelty
* Negative association after ragebait
* Increased aversion following regret
* Return to baseline after time away

Content-induced preference change must be separate from short-lived session fatigue.

### 4.17 User retention

Model the delay until the next session as a function of:

* Previous session satisfaction
* Previous session regret
* Habit strength
* Baseline usage frequency
* External schedule
* Accumulated platform trust

Measure:

* One-day simulated return
* Seven-day simulated retention
* Sessions per user
* Satisfaction-weighted retention
* Churn probability
* Long-term interest diversity

### 4.18 Ecosystem failure modes

Add experiments for:

* Filter-bubble formation
* Ragebait amplification
* Popularity feedback loops
* Niche-content starvation
* Creator overconcentration
* Exploration recovery
* Satisfaction versus retention conflicts

### Acceptance criteria

* Recommender policy must be capable of altering future hidden preference distributions.
* Engagement optimization should be able to create measurable negative long-term outcomes.
* Exploration should help recover neglected interests in at least one designed scenario.
* All long-term metrics must be reported separately from immediate engagement.

---

## Tier 5 — Learned Multi-Objective Ranking

### Objective

Make the recommender infer behaviour from logs rather than relying entirely on manually selected
ranking weights.

### 4.19 Prediction targets

Train separate models for:

* Probability of completion
* Expected watch ratio
* Probability of like
* Probability of share
* Probability of follow
* Probability of not-interested
* Probability of session exit
* Predicted satisfaction proxy

The true satisfaction value remains unavailable for ordinary training. A noisy survey or
explicit-feedback mechanism may expose a sampled subset.

### 4.20 Initial model choices

Begin with models that remain explainable:

1. Logistic regression for binary outcomes
2. Linear regression for watch ratio
3. Gradient-boosted trees as an optional comparison
4. Small neural model only after baselines are established

### 4.21 Multi-objective value function

```text
value =
    w_watch * predictedWatch
  + w_share * predictedShare
  + w_follow * predictedFollow
  + w_satisfaction * predictedSatisfaction
  - w_exit * predictedFailureExit
  - w_regret * predictedRegret
```

Run policies with different weight vectors to expose objective trade-offs.

### 4.22 Logged-data bias

The training process must record:

* Which candidates were eligible
* Which candidates were retrieved
* Which candidates were ranked
* Their positions
* Exploration probability
* Which candidates were actually shown

This supports future studies of exposure and position bias.

### Acceptance criteria

* Learned models outperform fixed baselines on held-out observable outcomes.
* Training data must not contain hidden simulator-only state.
* Model comparisons must use identical generated worlds and random streams.
* Position and exposure metadata must be persisted.
* Performance must be reported both offline and in closed-loop simulation.

---

# 5. Proposed Data Model Changes

## Reel

Add:

* Separate modality embeddings
* Usefulness
* Humour
* Novelty
* Emotional tone
* Controversy
* Clickbait strength
* Information density
* Language
* Format/category
* Optional hidden archetype parameters used only by the simulator

## HiddenUserState

Add:

* Per-modality preferences
* Content-value preferences
* Susceptibility traits
* Novelty preference
* Controversy tolerance
* Habit strength
* Platform trust
* Baseline daily usage
* Long-term preference plasticity

## User

Add only recommender-visible estimates:

* Estimated modality preferences
* Estimated novelty tolerance
* Estimated creator/topic fatigue
* Recent-session summary
* Last active timestamp
* Predicted exit risk
* Optional sampled explicit-feedback history

## InteractionEvent

Add:

* Position in feed
* Feed/request identifier
* Request timestamp
* Start and finish timestamp
* Dwell time
* Replay count
* Save/comment/profile-visit signals
* Whether the impression came from exploration
* Candidate-source provenance
* Observed session exit after impression

Do not include hidden satisfaction directly.

---

# 6. Evaluation Framework

Every experiment should report four metric groups.

## Engagement

* Watch seconds
* Completion
* Rewatch
* Likes
* Shares
* Follows
* Saves/comments if implemented

## Hidden user welfare

* Immediate satisfaction
* Regret
* Satisfaction per minute
* Harmful fatigue
* Platform trust
* Long-term preference distortion

## Session health

* Session duration
* Early failure exits
* Natural exits
* Return delay
* Sessions per simulated day
* Retention/churn

## Recommendation quality

* True affinity
* Estimated-to-hidden alignment
* Diversity
* Novelty
* Creator concentration
* Topic concentration
* Adaptation delay
* Exposure fairness

No single aggregate metric should replace these groups. A policy may improve one while damaging
another.

---

# 7. Determinism and Testing

Maintain named independent RNG streams for:

* User scheduling
* Behaviour
* Hidden satisfaction
* Session exit
* External interruption
* Preference change
* Recommender exploration
* Explicit-feedback sampling
* Model training data splitting

Required tests:

* Hidden-state type cannot be imported by recommender modules
* Same seed produces identical event sequence
* Equal-timestamp events have deterministic ordering
* Fatigue increases and decays correctly
* Creator/topic penalties do not leak hidden state
* Ragebait can produce high engagement and negative satisfaction
* Useful content can produce high satisfaction with weak engagement
* Early poor recommendations increase failure-exit probability
* Batch-size-one adapts no slower than larger stale batches
* Logged features contain no hidden labels except designated evaluation outputs

---

# 8. Vector Database Impact

## No vector-db modification required for Tiers 1–4

The existing vector database can continue indexing a fixed-size retrieval embedding.

The following belong entirely in ReelRank:

* Rich reel metadata
* Multiple hidden user preferences
* Satisfaction and regret
* Fatigue and repetition
* Independent clocks
* Event queue
* Session exits and returns
* Feed refresh
* Exposure-driven preference change
* Learned ranking over retrieved candidates

A reel can carry several embeddings while only one selected retrieval embedding is stored in HNSW.

## Potential vector-db work in Tier 5 or later

Vector-db modification is necessary only if ReelRank requires one of these capabilities:

### 1. Mutable learned reel embeddings

If reel embeddings are periodically retrained and must change in place, the current
immutable-index assumption becomes limiting.

Preferred initial solution:

* Build a new index offline
* Atomically replace the active index
* Keep the old index available during rebuild

This does not require in-place vector updates.

### 2. Multiple ANN indexes

Music, visual and semantic retrieval can initially use separate HNSW index instances through the
existing API.

No vector-db modification is required unless the library assumes one global schema or cannot host
multiple indexes cleanly.

### 3. Metadata filtering inside ANN search

Filtering by language, safety eligibility or active status can happen after retrieval initially.

At scale, pre-filtered or filter-aware HNSW search may improve recall and efficiency. That would
require vector-db support for:

* Search-time predicates
* Metadata bitmaps
* Allowed-ID filters

This is not required for the simulator-realism phases.

### 4. Deletion and expiration

Reels can initially be marked inactive and filtered after retrieval.

True deletions, updates and compaction would require vector-db changes, but are unnecessary for
this TDD.

### Recommendation

Keep vector-db unchanged throughout Tiers 1–4.

Treat the ANN layer as a stable candidate-retrieval component. Only modify it after experiments
demonstrate that immutable embeddings, post-filtering or multiple-index management create a
measurable limitation.

---

# 9. Recommended Delivery Order

## Release A — Satisfaction Model

Implement Tier 1 only.

This creates the strongest conceptual improvement and establishes engagement-versus-satisfaction
experiments.

## Release B — Session Health

Implement Tier 2.

Make exits, fatigue and returns meaningful outcomes.

## Release C — Event Simulator

Implement Tier 3.

Move from round-robin rounds to independent user clocks and configurable feed prefetching.

## Release D — Long-Term Effects

Implement Tier 4.

Study preference shaping, retention and harmful optimization loops.

## Release E — Learned Ranking

Implement Tier 5 after the simulator's hidden world is stable.

Training models before stabilizing the synthetic environment risks learning against an
ever-changing target.

---

# 10. Definition of Completion

The realism upgrade is complete when ReelRank can demonstrate all of the following:

1. High engagement can coexist with low satisfaction.
2. High satisfaction can occur with modest observable engagement.
3. Poor recommendation sequences cause early failure exits.
4. Repetition affects users differently according to hidden traits.
5. Users operate on independent timelines.
6. Feed batch depth produces a measurable adaptation trade-off.
7. Ranking policy changes long-term satisfaction and retention.
8. The recommender learns only from observable data.
9. Experiment results remain deterministic and reproducible.
10. The existing vector database remains unchanged unless a measured ANN limitation justifies
    modifying it.
