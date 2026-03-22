//
// Created by lenny on 21.03.2026.
//

#ifndef BYTEBUDDY_BUDDY_HPP
#define BYTEBUDDY_BUDDY_HPP

#include <array>
#include <cmath>
#include <string>
#include <vector>
#include <deque>
#include <random>
#include <algorithm>
#include <unordered_map>

#include <SDL3/SDL.h>
#include <JFLX/SDL3/textureHandler.hpp>
#include <JFLX/SDL3/textRenderer.hpp>

namespace ByteBuddy {

    /// Module-level RNG, seeded once from hardware entropy.
    static std::mt19937 rng{ std::random_device{}() };

    // -------------------------------------------------------------------------

    /// Usable area of the application window.
    /// `ground` is the Y coordinate the Buddy snaps back to when not jumping.
    struct windowBounds {
        int height = 0;
        int width  = 0;
        int ground = 0;
    };

    // -------------------------------------------------------------------------

    /**
     * @brief Seven animation frames that make up a single jump arc.
     *
     * Each value maps 1-to-1 to a sprite frame index (jump_0 … jump_6).
     * The active phase is driven by the normalised t-value of the current
     * trajectory step — see updateTrajectory().
     */
    enum JumpPhase {
        PreJump,    ///< Frame 0 – crouching, preparing to leap          (t ∈ [0.00, 0.02))
        MidJump,    ///< Frame 1 – leaving the ground, rising fast        (t ∈ [0.10, 0.30))
        MidAir,     ///< Frame 2 – at the apex, horizontal body           (t ∈ [0.30, 0.65))
        MidFall_0,  ///< Frame 3 – descending, upper portion of the fall  (t ∈ [0.55, 0.75))
        MidFall_1,  ///< Frame 4 – descending, lower portion of the fall  (t ∈ [0.75, 0.92))
        Landing_0,  ///< Frame 5 – feet touching down                     (t ∈ [0.90, 0.98))
        Landing_1,  ///< Frame 6 – legs fully extended, last landing frame (t ∈ [0.97, 1.00])
    };

    // -------------------------------------------------------------------------

    /**
     * @brief All possible behavioural states.
     *
     * States are chosen randomly via randomState() and held for
     * b_stateTimerMin … b_stateTimerMax ticks before the next one is picked.
     *
     * Texture key convention: `<buddyName>_<stateName>_<frameIndex>`
     * e.g. "cat_walk_0", "cat_idle_1_2", "cat_jump_3"
     */
    enum class BuddyState {
        Idle_0,  ///< Sitting still / blinking — no movement
        Idle_1,  ///< Looking around — no movement
        Clean_0, ///< Grooming, part 1 — no movement, direction not updated
        Clean_1, ///< Grooming, part 2 — no movement, direction not updated
        Walk,    ///< Walks toward b_targetX at 2 × speed px/tick
        Run,     ///< Runs toward b_targetX at 4 × speed px/tick
        Jump,    ///< Parabolic arc driven by b_trajectory; direction IS updated
        Sleep,   ///< Resting — no movement, direction not updated
    };

    // -------------------------------------------------------------------------

    /// One keyframe inside a pre-computed jump trajectory.
    /// updateBuddy() pops the front entry each tick while in Jump state.
    struct target {
        std::array<float, 2> pos;              ///< World-space {x, y} for this keyframe
        JumpPhase phase = JumpPhase::PreJump;  ///< Which jump sprite to display here
    };

    // -------------------------------------------------------------------------

    [[nodiscard]] std::string getCatStateName(BuddyState state);
    BuddyState getCatStateFromName(const std::string& name);

    /// Returns a uniformly random BuddyState drawn from the full range.
    BuddyState randomState();

    // -------------------------------------------------------------------------

    /**
     * @class Buddy
     * @brief The animated desktop companion sprite.
     *
     * ## Lifecycle
     * 1. Construct a `Buddy`.
     * 2. Call setName, setTextureHandler, setTextRenderer, setWindowBounds,
     *    setPosition before the main loop.
     * 3. Call updateBuddy(mouseX, mouseY) once per frame.
     * 4. Call renderBuddy() once per frame after updating.
     *
     * ## Texture naming
     * Non-jump states: `<name>_<stateName>_<frameIndex>`
     * Jump state:      `<name>_jump_<0..6>` (one per JumpPhase value)
     *
     * Frame indices are probed dynamically; the counter wraps to 0 as soon as
     * the next-index texture is missing from the handler.
     *
     * ## Direction convention
     * b_direction == -1  →  facing left  (no horizontal flip)
     * b_direction == +1  →  facing right (SDL_FLIP_HORIZONTAL)
     */
    class Buddy {
    private:
        JFLX::SDL3::TextRenderer*   b_textRenderer = nullptr;
        JFLX::SDL3::TextureHandler* b_texHandler    = nullptr;

        windowBounds b_windowBounds;

        std::string b_name         = "cat";
        std::string b_typeName     = "cat";
        float       b_x        = 0.f;
        float       b_y        = 0.f;
        float       b_textureW = 0.f;  ///< Set each frame by updateTextureSize()
        float       b_textureH = 0.f;
        float       b_speed    = 1.f;  ///< Walk = 2×, Run = 4× this value (px/tick)
        float       b_scale    = 4.f;

        /// Walk/Run destination. Chosen randomly when entering those states
        /// (25 % chance: mouseX, 75 % chance: random X within window width).
        float b_targetX = 0.f;
        float b_targetY = 0.f;

        float b_stateTimer   = 0.f;
        float b_stateTimeMax = 300.f;

        int b_stateTimerMin = 100;  ///< Lower bound for random state duration (ticks)
        int b_stateTimerMax = 600;  ///< Upper bound for random state duration (ticks)

        /// -1 = left, +1 = right. Multiplied into movement deltas so both
        /// directions produce symmetric speeds.
        int b_direction = 1;

        int   b_frame         = 0;
        float b_frameTimer    = 0.f;
        float b_frameInterval = 2.f;  ///< Ticks between frame advances (2 ≈ 30 fps at 60 fps)

        bool b_updateDirectionWhenIdle = true;
        bool b_horizontallyFlipped = false;

        /// Pre-computed keyframes for the current jump arc.
        /// deque used so pop_front() is O(1).
        std::deque<target> b_trajectory;

        BuddyState b_state     = BuddyState::Idle_0;
        JumpPhase  b_jumpPhase = JumpPhase::PreJump;  ///< Only meaningful during Jump state

    public:
        Buddy() = default;

        // --- Texture / animation helpers -------------------------------------

        /// Returns the texture key to render this frame.
        /// Non-jump: `<name>_<stateName>_<frame>`, Jump: `<name>_jump_<phaseIndex>`.
        [[nodiscard]] std::string getTextureName() const;

        /// Returns true if `ani_name` is a known animation token.
        /// Checks a hard-coded list — does NOT query the texture handler.
        static bool hasAnimation(const std::string& ani_name);

        /// Switches to the named animation and resets b_frame / b_frameTimer.
        /// Falls back to Idle_0 for unknown names.
        void setAnimation(const std::string& ani_name);

        // --- Per-frame update logic ------------------------------------------

        /**
         * @brief Fills b_trajectory with a smooth parabolic arc from the current
         *        position to (targetX, windowBounds.ground).
         *
         * Step count scales with horizontal distance (|dx| × 0.25, min 15).
         * Jump height = |dx| × 0.6, clamped to [30, 120] px.
         * Each entry stores the world-space position and the JumpPhase
         * whose t-range contains that step's normalised arc position.
         */
        void updateTrajectory(float groundY);

        /// Updates b_direction so the Buddy faces b_targetX.
        /// Skipped for Clean_0, Clean_1, and Sleep (flipping mid-clip looks wrong).
        void updateDirection();

        /**
         * @brief Main per-frame update — call exactly once per game-loop tick.
         *
         * Order each tick:
         * 1. Advance frame animation (wraps when next texture is missing).
         * 2. Advance state timer; pick a new random state + duration when it expires.
         *    - Jump: requires mouse ≥ 160 px away; falls back to random state otherwise.
         *    - Walk/Run: picks a new random target; falls back if already at destination.
         * 3. Move: Jump consumes one trajectory entry; Walk/Run advance toward b_targetX.
         * 4. Gravity: drift toward ground at 2 px/tick, clamp to windowBounds.ground.
         * 5. Speech bubble: "MEOW" drawn above the Buddy when mouse is within 20 px.
         */
        void updateBuddy(float mouseX, float mouseY);

        /// Renders the sprite centered on (b_x, b_y), scaled by b_scale.
        /// Applies SDL_FLIP_HORIZONTAL when b_direction == -1 (facing left).
        void renderBuddy() const;

        void nextState();

        void setName(const std::string &name);

        /// Queries b_textureW / b_textureH from the texture handler each frame.
        /// Must be called before renderBuddy() because sprite sizes can vary per state.
        void updateTextureSize();

        // --- Setters ---------------------------------------------------------

        void setTypeName(const std::string& name);
        void setTextureHandler(JFLX::SDL3::TextureHandler* th);
        void setTextRenderer(JFLX::SDL3::TextRenderer* tr);
        void setWindowBounds(const windowBounds bounds);

        /// Overrides the current state. Does NOT reset b_frame — use setAnimation() for that.
        void setState(BuddyState state, float mouseX, float mouseY);

        void setPosition(float nx, float ny);
        void setSpeed(float s);
        void setScale(float s);
        void setGroundY(float y);

        /// Positive values → +1 (right), negative → -1 (left).
        void setDirection(int dir);

        void setStateTimer(float ticks);
        void setStateTimerMin(float ticks);
        void setStateTimerMax(float ticks);
        void setStateTimeMax(float ticks);

        void setHorizontallyFlipped(bool flip);
        void setUpdateDirectionWhenIdle(bool flip);

        std::string getName() const;

        /// Ticks between frame advances. Lower = faster animation. Must be > 0.
        void setFrameInterval(float ticks);

        // --- Getters ---------------------------------------------------------

        [[nodiscard]] float       getX()                    const;
        [[nodiscard]] float       getY()                    const;
        [[nodiscard]] std::string getTypeName()             const;
        [[nodiscard]] std::string getState()                const;
        [[nodiscard]] JumpPhase   getJumpPhase()            const;
        [[nodiscard]] float       getSpeed()                const;
        [[nodiscard]] float       getScale()                const;
        [[nodiscard]] float       getStateTimeMax()         const;
        [[nodiscard]] int         getStateTimerMax()        const;
        [[nodiscard]] int         getStateTimerMin()        const;
        [[nodiscard]] float       getFrameInterval()        const;
        [[nodiscard]] int         getDirection()            const;
        [[nodiscard]] int         getFrame()                const;
        [[nodiscard]] int         getGroundY()              const;
        [[nodiscard]] std::array<int, 2> getTarget()        const;

        bool getHorizontallyFlipped() const;
        bool getUpdateDirectionWhenIdle() const;
    };

} // namespace ByteBuddy

#endif // BYTEBUDDY_BUDDY_HPP