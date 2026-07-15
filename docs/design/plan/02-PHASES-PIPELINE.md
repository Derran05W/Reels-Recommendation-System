# ReelRank Plan — Part 2: Recommendation Pipeline (Phases 5–9)

One phase = one Claude session. Read `plan/00-DESIGN-DECISIONS.md`, this file's phase section,
`commit.md`, and the referenced TDD sections before starting. Follow the D1 session protocol.

---

## Phase 5 — HNSW retrieval in the feed pipeline

**TDD refs:** §12.1, §13, §16.4, §18.1, §28 Phase 5.
**Prerequisites:** Phases 1, 4.

### Objective
HNSW candidate retrieval integrated into a real orchestrated pipeline, with exact-vs-HNSW
recommendation quality and latency compared and published.

### Tasks
1. `HNSWCandidateSource` implementing `CandidateGenerator`: queries `HNSWVectorIndex` with the user's effective preference for top-N (default 500), fills `Candidate{source=VectorHNSW, retrievalDistance, retrievalSimilarity}` via D3 conversion. `ExactCandidateSource` mirror for ground truth.
2. `Orchestrator` v1 (TDD §7/§13): run configured sources → merge → dedup by reel id **preserving all source labels** → drop inactive/seen/invalid → cap pool size. Per-stage latency captured into `RecommendationResponse` (retrieval/ranking/reranking/total — ranking is identity for now: sort by similarity).
3. `HNSWRecommender` (TDD §16.4: similarity ordering only) using the orchestrator.
4. Retrieval metrics module (TDD §18.1): Recall\@K vs exact on live requests (sampled, rate config-driven), distance error. Wire into the harness output (`retrieval_metrics.csv`).
5. Tests: unit (dedup preserves multi-source labels; filter rules; pool cap; candidate counts never exceed configured limits — property); integration (HNSW and Exact recommenders on identical seed/dataset: report overlap; end-to-end request populates all latency fields); property (HNSW results contain no invalid/inactive ids over many seeds); determinism.
6. Experiment: `configs/medium.json` (100k reels / 10k users), HNSWRecommender vs ExactVectorRecommender — reward delta, avg-true-affinity delta, live Recall\@10/@50, retrieval p50/p95/p99 both ways. Publish to `results/published/phase5/`. This is the project's core engineering question (TDD §3) — make the numbers prominent.

### Exit criteria (TDD §28 P5)
- [ ] HNSW integrated into the pipeline; feeds served through the orchestrator.
- [ ] Retrieval quality (live recall) and latency reported.
- [ ] HNSW-vs-exact reward comparison published on the medium config.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/02-PHASES-PIPELINE.md (Phase 5), commit.md, TDD §§12.1, 13, 16.4, 18.1. Implement HNSW/Exact candidate sources, the Orchestrator with merge/dedup/filter and per-stage latency, and the HNSWRecommender, test-first. Run the medium-config HNSW-vs-exact experiment and publish it. Update commit.md per protocol.

---

## Phase 6 — Second-stage ranker

**TDD refs:** §12.3–12.6, §14, §16.5, §28 Phase 6.
**Prerequisites:** Phase 5.

### Objective
Feature-based deterministic ranking over merged candidates, with inspectable per-feature
contributions, plus the candidate sources that feed its features.

### Tasks
1. Remaining scoring inputs / candidate sources (needed as ranking features and useful as sources):
   - `TrendingCandidateSource` + trending score with exponential time decay (TDD §12.4) — maintain a decayed interaction accumulator on reels updated by `Simulator::step`;
   - `CreatorAffinityCandidateSource` (TDD §12.6) — reads `User::creatorAffinity` (updated on follow/like in Phase 3's step; verify and extend if missing);
   - freshness score with configurable decay from `createdAt` (TDD §8.2).
2. `FeatureExtractor`: per-candidate features of TDD §14.1 (similarity, quality, freshness, popularity, trending, creator affinity, duration-preference match, previous impressions, repetition penalty; exploration bonus = 0 until Phase 8; session-topic similarity = 0 until Phase 7). Deterministic, documented normalization into [0,1] per feature (min-max over pool for pool-relative features, sigmoid/log for counts — TDD §14.3).
3. `WeightedRanker` implementing `Ranker`: TDD §14.2 formula, weights from `RankingConfig`, writes `featureContributions` on every candidate (TDD §14.4). Ranking-explanation JSON dump in `inspect_user` (extend the app: show a user's feed with contribution breakdown).
4. `HNSWRankerRecommender` (TDD §16.5): orchestrator + all sources (HNSW 500, popular 100, trending 100, creator 100 — TDD §13 counts) + WeightedRanker.
5. Tests: unit (each normalization rule; each feature; weight config plumbing; score formula against hand-computed cases; trending decay math); property (ranking output monotonically non-increasing by score pre-diversity; contributions sum ≈ score); integration (ranked feed end-to-end; explanation output well-formed); statistical (ranker beats raw-similarity HNSW recommender on reward — the TDD §3 "how much does second-stage ranking improve" question).
6. Experiment on medium config: HNSW-only vs HNSW+ranker. Publish `results/published/phase6/`.

### Exit criteria (TDD §28 P6)
- [ ] Ranker processes HNSW candidates; contributions inspectable per item.
- [ ] HNSW+ranker vs HNSW-only comparison published.
- [ ] All feature normalizations deterministic, documented in code, and unit-tested.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/02-PHASES-PIPELINE.md (Phase 6), commit.md, TDD §§12.3–12.6, 14, 16.5. Implement trending/creator-affinity sources, the FeatureExtractor with documented normalizations, the WeightedRanker with feature contributions, and the HNSW+ranker recommender, test-first. Publish the HNSW-only vs HNSW+ranker comparison. Update commit.md per protocol.

---

## Phase 7 — Online preference learning

**TDD refs:** §8.3, §11.1–11.3, §16 (session-aware variants), §18.5, §28 Phase 7.
**Prerequisites:** Phase 6 (runs after ranked feeds exist; technically only needs Phase 4).

### Objective
Users' estimated preferences update from interactions: long-term + session vectors, effective
preference blending, measurable learning curves.

### Tasks
1. `OnlineUserStateUpdater` implementing `UserStateUpdater` (TDD §23.4):
   - long-term update `u ← normalize((1−η)u + η·r·v)`, η=0.02 (TDD §11.2), negative reward pushes away;
   - session vector from λ-decayed recent-interaction window (λ config, window 20, TDD §11.3);
   - maintains `recentInteractions` deque, `seenReels`, `creatorAffinity`, interaction counters.
2. `effectivePreference = 0.65·longTerm + 0.35·session` (normalized, weights config-driven, TDD §8.3) — used by vector candidate sources from now on; session-topic-similarity ranking feature activated.
3. Session lifecycle: session id/length management, session vector reset between sessions (simulator-driven).
4. Learning-curve output: `learning_curve.csv` (reward and estimated↔hidden cosine distance vs interaction count, bucketed).
5. Tests: unit (update math incl. normalization and negative reward; window eviction; λ decay; blend weights); property (updates never touch `HiddenUserState` — compile-time by D11 plus a runtime assertion test; estimated preference stays unit-length); statistical/integration (fresh users: estimated→hidden cosine similarity increases over 200 interactions; reward per impression improves over time; feeding only disliked-topic content moves the estimate away); determinism.
6. Experiment: cold-start learning curves (TDD §18.5: interactions to reach target reward, regret over first 10/25/50/100 impressions) for HNSW+ranker with learning on vs frozen estimates. Publish `results/published/phase7/`.

### Exit criteria (TDD §28 P7)
- [ ] New users improve over time (published curve + test).
- [ ] Estimate converges toward hidden preference under positive feedback; diverges from disliked content (tests).
- [ ] Session/long-term split works and is config-driven.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/02-PHASES-PIPELINE.md (Phase 7), commit.md, TDD §§8.3, 11, 18.5. Implement OnlineUserStateUpdater (long-term + session + effective preference), wire it into the simulation loop and vector sources, test-first including convergence statistics. Publish cold-start learning curves. Update commit.md per protocol.

---

## Phase 8 — Exploration and cold start

**TDD refs:** §12.5, §12.7, §16.7, §18.5, §19, §28 Phase 8.
**Prerequisites:** Phase 7.

### Objective
Epsilon-greedy exploration and fresh-content candidates, with measured impact on regret, new-user
learning, and new-reel exposure.

### Tasks
1. `FreshCandidateSource` (TDD §12.5): recently-created reels, optional topic-proximity filter to user's estimate.
2. `ExplorationCandidateSource` (TDD §12.7): with probability ε per slot — random fresh reels, underexposed reels (low impression count), uncertain-topic reels (topics distant from the user's estimate). ε=0.05 default, config-driven.
3. Exploration-bonus ranking feature activated (weight from config); exploration candidates protected from being ranked out entirely (guaranteed minimum slots, config-driven, document the rule).
4. `HNSWExplorationRecommender` (TDD §16.7).
5. Cold-start experiment support: inject users/reels mid-simulation (`newUsersAt`, `newReelsAt` config hooks) so genuinely-cold entities are measurable; new-reel exposure metric (impressions gained by reels created after simulation start, TDD §18.5).
6. Tests: unit (epsilon gating from forked stream; underexposed selection; fresh recency window); property (exploration candidates ≤ configured count; ε=0 ⇒ output identical to non-exploring recommender under same seed); integration (new reels receive nonzero exposure with exploration on, ~zero with it off); determinism.
7. Experiments: (a) ε sweep {0, 0.02, 0.05, 0.1} → cumulative regret + new-reel exposure; (b) new-user regret with vs without exploration. Publish `results/published/phase8/`.

### Exit criteria (TDD §28 P8)
- [ ] Exploration rate configurable; ε=0 is a verified no-op.
- [ ] New users receive mixed content; new reels get measurable exposure.
- [ ] Exploration impact on regret published.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/02-PHASES-PIPELINE.md (Phase 8), commit.md, TDD §§12.5, 12.7, 16.7, 18.5, 19. Implement Fresh and Exploration candidate sources, epsilon-greedy gating, mid-simulation entity injection, test-first. Run the epsilon sweep and cold-start experiments and publish. Update commit.md per protocol.

---

## Phase 9 — Diversity re-ranking

**TDD refs:** §15, §16.6, §18.4, §28 Phase 9.
**Prerequisites:** Phase 6 (ranked feeds); Phase 8 recommended.

### Objective
Constraint-based diversity re-ranking plus MMR, with the engagement-vs-diversity trade-off
measured.

### Tasks
1. `ConstraintReranker` implementing `Reranker` (TDD §15.1): no duplicate ids, ≤2 per creator, ≤3 per primary topic per 10-item feed (scaled proportionally to feedSize), no seen reels, avoid consecutive same-topic where possible (greedy swap). Deterministic tie-breaking (by score then id).
2. `MMRReranker` (TDD §15.2): λ=0.75 default, embedding cosine as the inter-item similarity; composable after constraints (constraints are hard rules, MMR orders within them — document this composition decision).
3. Diversity metrics module (TDD §18.4): unique topics/creators per feed, intra-list embedding similarity, topic/creator concentration (HHI), repetition rate → `diversity_metrics.csv`.
4. `FullRecommender` (TDD §16.6: HNSW + ranker + diversity; with exploration = the complete initial system).
5. Tests: unit (each constraint individually and combined; MMR math on hand-built cases; feed shorter than requested only when pool exhausted — documented behaviour); property (no feed ever violates caps or contains seen/duplicate ids, across many seeds — this is the flagship property test); integration + determinism.
6. Experiment on medium config: HNSW+ranker vs +constraints vs +constraints+MMR — diversity metrics vs reward/engagement trade-off (TDD §3). Publish `results/published/phase9/`.

### Exit criteria (TDD §28 P9)
- [ ] Duplicate/repetitive content eliminated (property-tested).
- [ ] Diversity metrics improve vs Phase 6 baseline (published).
- [ ] Engagement trade-off quantified and published.

### Session kickoff prompt
> Read plan/00-DESIGN-DECISIONS.md, plan/02-PHASES-PIPELINE.md (Phase 9), commit.md, TDD §§15, 16.6, 18.4. Implement ConstraintReranker and MMRReranker test-first with the no-violations property suite, add diversity metrics, assemble the FullRecommender, and publish the diversity-vs-engagement comparison. Update commit.md per protocol.
