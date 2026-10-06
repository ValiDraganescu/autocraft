// Members of Commander+Requests.swift (bodies in
// Private/Commander+Requests.cpp). Included inside `struct Commander` by
// Commander.h; the small types Swift nests in `Commander` are nested here too.

    // The team's queue (`Directives`): what its humans asked the AI to spend
    // on next. Every team's `Commander` runs this the same way.

    /// What one request needs done now.
    struct Step {
        /// Give this order (paid `ore`/`hydrogen`); `serves` when it is the
        /// request itself rather than something it needs first. `note`
        /// says what it is when it is something first.
        struct Order {
            Command command;
            int64_t ore = 0;
            int64_t hydrogen = 0;
            int64_t supply = 0;
            bool serves = false;
            std::optional<std::string> note;
            std::optional<int64_t> worker;
        };
        /// Nothing to order yet; the request's cost is held meanwhile.
        struct Wait {
            std::string why;
        };
        /// It can no longer happen (already researched, unit locked).
        struct Drop {};

        std::variant<Order, Wait, Drop> value;
    };

    /// What `requests` returns (a tuple in Swift).
    struct Requests {
        std::vector<Command> out;
        std::vector<RequestStatus> status;
        struct Reserved {
            int64_t ore = 0;
            int64_t hydrogen = 0;
        } reserved;
    };

    /// What the AI orders for its team's queue this second, in queue order,
    /// out of `money` and `hydrogen`, and where each request stands. A request
    /// it cannot order yet holds its cost (`money`/`hydrogen` go down by it,
    /// below zero if need be), so the AI's own spending after it uses only
    /// what is left; `reserved` is what is held. `prior` are orders given
    /// this second already (a new building never takes their spot).
    Requests requests(const Simulation& sim, int64_t& money, int64_t& hydrogen, int64_t& supplyFree,
                      std::set<int64_t>& busy, const std::vector<Command>& prior = {}) const;

    /// Where each of the team's requests stands, for its humans.
    std::vector<RequestStatus> requestStatus(const Simulation& sim) const;

    /// The order a request's `.serve` carries, or the order itself.
    static Command inner(const Command& c);

    /// Of several buildings ordered on one spot in one second, only the
    /// first (the AI and a request may both pick the best spot).
    static std::vector<Command> oneBuildPerSpot(const std::vector<Command>& orders);
