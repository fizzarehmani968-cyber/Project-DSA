// ============================================================
//  CYBER ESCAPE : ANTIVIRUS HUNT   (SFML 3 version)
//
//  DATA STRUCTURES USED
//   GRAPH  : maze as adjacency list (cell = node, open neighbour = edge)
//   QUEUE  : BFS -> enemy chasing, hints, farthest-exit search
//   STACK  : (1) maze generation (recursive backtracker)
//            (2) undo move history
//
//  CONTROLS
//   Arrows / WASD = move      H = hint (BFS)     U = undo (stack pop)
//   G = graph x-ray view      ENTER = start/next level     ESC = quit
//
//  GOAL: collect ALL data chips, then reach the green exit.
//        Avoid the red antivirus bugs. Don't run out of time.
// ============================================================
#include <SFML/Graphics.hpp>
#include <vector>
#include <queue>
#include <stack>
#include <algorithm>
#include <string>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <random>
#include <optional>
using namespace std;

const int ROWS = 15, COLS = 21;     // must be odd numbers (maze generator needs it)
const int CELL = 36, HUD_H = 50;
const int W = COLS * CELL;
const int H = ROWS * CELL + HUD_H;

mt19937 rng((unsigned)time(nullptr));
int randInt(int a, int b) { return uniform_int_distribution<int>(a, b)(rng); }

// ---------------- STACK: random maze generation ----------------
vector<vector<int>> generateMaze() {
    vector<vector<int>> m(ROWS, vector<int>(COLS, 1));   // 1 = wall
    stack<pair<int,int>> st;                             // STACK (backtracking)
    m[1][1] = 0;
    st.push({1, 1});
    int dr[] = {-2, 2, 0, 0};
    int dc[] = {0, 0, -2, 2};

    while (!st.empty()) {
        pair<int,int> cur = st.top();
        int r = cur.first, c = cur.second;
        vector<int> options;
        for (int k = 0; k < 4; k++) {
            int nr = r + dr[k], nc = c + dc[k];
            if (nr >= 1 && nr < ROWS - 1 && nc >= 1 && nc < COLS - 1 && m[nr][nc] == 1)
                options.push_back(k);
        }
        if (options.empty()) { st.pop(); continue; }     // dead end -> backtrack
        int k = options[randInt(0, (int)options.size() - 1)];
        m[r + dr[k] / 2][c + dc[k] / 2] = 0;             // knock down wall between
        m[r + dr[k]][c + dc[k]] = 0;
        st.push({r + dr[k], c + dc[k]});
    }

    // Remove some extra walls so the maze has loops (cycles) -> enemies can flank you
    int tries = (ROWS * COLS) / 10;
    for (int i = 0; i < tries; i++) {
        int r = randInt(1, ROWS - 2), c = randInt(1, COLS - 2);
        if (m[r][c] == 1) {
            bool vert  = (m[r - 1][c] == 0 && m[r + 1][c] == 0);
            bool horiz = (m[r][c - 1] == 0 && m[r][c + 1] == 0);
            if (vert || horiz) m[r][c] = 0;
        }
    }
    return m;
}

// ---------------- GRAPH: adjacency list ----------------
vector<vector<int>> buildGraph(const vector<vector<int>>& maze) {
    vector<vector<int>> adj(ROWS * COLS);
    int dr[] = {-1, 1, 0, 0};
    int dc[] = {0, 0, -1, 1};
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (maze[r][c] == 1) continue;
            for (int k = 0; k < 4; k++) {
                int nr = r + dr[k], nc = c + dc[k];
                if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && maze[nr][nc] == 0)
                    adj[r * COLS + c].push_back(nr * COLS + nc);
            }
        }
    return adj;
}

// ---------------- QUEUE: BFS shortest path ----------------
vector<int> bfsPath(const vector<vector<int>>& adj, int start, int goal) {
    vector<int> parent(adj.size(), -1);
    vector<bool> visited(adj.size(), false);
    queue<int> q;                                        // QUEUE
    q.push(start);
    visited[start] = true;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        if (u == goal) break;
        for (int v : adj[u])
            if (!visited[v]) {
                visited[v] = true;
                parent[v] = u;
                q.push(v);
            }
    }
    vector<int> path;
    if (!visited[goal]) return path;
    for (int at = goal; at != -1; at = parent[at]) path.push_back(at);
    reverse(path.begin(), path.end());
    return path;
}

// ---------------- QUEUE: BFS distance from one node to all nodes ----------------
vector<int> bfsDist(const vector<vector<int>>& adj, int start) {
    vector<int> dist(adj.size(), -1);
    queue<int> q;
    q.push(start);
    dist[start] = 0;
    while (!q.empty()) {
        int u = q.front();
        q.pop();
        for (int v : adj[u])
            if (dist[v] == -1) {
                dist[v] = dist[u] + 1;
                q.push(v);
            }
    }
    return dist;
}

struct Enemy    { int r, c; float vr, vc; };   // grid position + smooth visual position
struct Particle { sf::Vector2f pos, vel; float life; sf::Color color; };
enum State { MENU, PLAYING, LEVEL_DONE, GAME_OVER };

int main() {
    sf::RenderWindow window(sf::VideoMode({(unsigned)W, (unsigned)H}), "Cyber Escape - Antivirus Hunt");
    window.setFramerateLimit(60);

    sf::Font font;
    bool hasFont = font.openFromFile("C:/Windows/Fonts/consola.ttf") ||
                   font.openFromFile("arial.ttf") ||
                   font.openFromFile("C:/Windows/Fonts/arial.ttf");

    // ---------- game variables ----------
    State state = MENU;
    int level = 1, lives = 3, score = 0;
    vector<vector<int>> maze = generateMaze();
    vector<vector<int>> graph = buildGraph(maze);
    int playerR = 1, playerC = 1;
    float pvr = 1, pvc = 1;                       // smooth visual position
    int exitR = 1, exitC = 1;
    stack<pair<int,int>> history;                 // STACK for undo
    vector<pair<int,int>> chips;
    int chipsTotal = 0;
    vector<Enemy> enemies;
    vector<Particle> particles;
    vector<sf::Vector2f> trail;
    vector<int> cells, hintPath;
    float timeLeft = 0, enemyTimer = 0, enemyInterval = 0.3f, moveCooldown = 0, fogRadius = 6.5f;
    float hintTimer = 0, flash = 0, shake = 0, invuln = 0, msgTimer = 0, totalTime = 0;
    int hintTarget = 0, hintsLeft = 3, undosLeft = 3, lastBonus = 0;
    bool showGraph = false;
    string msg;

    vector<float> rainY(COLS * 2), rainSpeed(COLS * 2);
    for (int i = 0; i < COLS * 2; i++) {
        rainY[i] = (float)randInt(0, ROWS * CELL);
        rainSpeed[i] = (float)randInt(50, 140);
    }

    // ---------- helper lambdas ----------
    auto drawText = [&](const string& s, float x, float y, unsigned size, sf::Color col, bool center) {
        if (!hasFont) return;
        sf::Text t(font, s, size);
        t.setFillColor(col);
        if (center) {
            sf::FloatRect b = t.getLocalBounds();
            t.setOrigin({b.position.x + b.size.x / 2.f, 0.f});
        }
        t.setPosition({x, y});
        window.draw(t);
    };

    auto glow = [&](float x, float y, float rad, sf::Color col) {
        for (int i = 3; i >= 1; i--) {
            sf::CircleShape c(rad * i * 0.7f);
            c.setOrigin({c.getRadius(), c.getRadius()});
            c.setPosition({x, y});
            c.setFillColor(sf::Color(col.r, col.g, col.b, (uint8_t)(25 * (4 - i))));
            window.draw(c);
        }
    };

    auto burst = [&](float x, float y, sf::Color col, int n) {
        for (int i = 0; i < n; i++) {
            float a = randInt(0, 628) / 100.f;
            float sp = (float)randInt(40, 160);
            Particle p;
            p.pos = sf::Vector2f(x, y);
            p.vel = sf::Vector2f(cos(a) * sp, sin(a) * sp);
            p.life = randInt(40, 90) / 100.f;
            p.color = col;
            particles.push_back(p);
        }
    };

    // enemies are placed far away from the player
    auto spawnEnemies = [&]() {
        enemies.clear();
        shuffle(cells.begin(), cells.end(), rng);
        int count = min(2 + level / 2, 6);
        for (int id : cells) {
            if ((int)enemies.size() >= count) break;
            int r = id / COLS, c = id % COLS;
            if (abs(r - playerR) + abs(c - playerC) >= 10) {
                Enemy e;
                e.r = r; e.c = c; e.vr = (float)r; e.vc = (float)c;
                enemies.push_back(e);
            }
        }
    };

    auto startLevel = [&]() {
        maze = generateMaze();
        graph = buildGraph(maze);
        playerR = 1; playerC = 1; pvr = 1; pvc = 1;
        while (!history.empty()) history.pop();
        trail.clear(); hintPath.clear(); particles.clear();
        hintTimer = 0; invuln = 0; enemyTimer = 0; moveCooldown = 0;

        // exit = the node FARTHEST from the start (found with BFS)
        vector<int> dist = bfsDist(graph, 1 * COLS + 1);
        int best = 1 * COLS + 1;
        cells.clear();
        for (int i = 0; i < (int)dist.size(); i++)
            if (dist[i] >= 0) {
                cells.push_back(i);
                if (dist[i] > dist[best]) best = i;
            }
        exitR = best / COLS; exitC = best % COLS;

        // data chips at random far-ish cells
        chips.clear();
        chipsTotal = min(2 + level, 6);
        shuffle(cells.begin(), cells.end(), rng);
        for (int id : cells) {
            if ((int)chips.size() >= chipsTotal) break;
            if (dist[id] < 5 || id == best) continue;
            chips.push_back({id / COLS, id % COLS});
        }

        spawnEnemies();

        // difficulty scales with level
        fogRadius = max(3.5f, 6.5f - 0.4f * (level - 1));
        enemyInterval = max(0.17f, 0.34f - 0.03f * level);
        timeLeft = max(50.f, 130.f - 8.f * level);
        undosLeft = 3;
        hintsLeft = 3;
        msg = "Collect all data chips, then reach the exit!";
        msgTimer = 3.f;
    };

    auto newGame = [&]() {
        level = 1; lives = 3; score = 0;
        startLevel();
        state = PLAYING;
    };

    auto loseLife = [&]() {
        lives--;
        flash = 1.f;
        shake = 0.35f;
        burst((playerC + 0.5f) * CELL, (playerR + 0.5f) * CELL, sf::Color(255, 60, 60), 30);
        if (lives <= 0) { state = GAME_OVER; return; }
        playerR = 1; playerC = 1; pvr = 1; pvc = 1;
        while (!history.empty()) history.pop();
        trail.clear();
        spawnEnemies();
        invuln = 1.5f;
        msg = "Caught by antivirus!";
        msgTimer = 1.5f;
    };

    auto checkHit = [&]() {
        if (invuln > 0 || state != PLAYING) return;
        for (auto& e : enemies)
            if (e.r == playerR && e.c == playerC) { loseLife(); return; }
    };

    auto useHint = [&]() {
        if (hintsLeft <= 0) { msg = "No hints left!"; msgTimer = 1.5f; return; }
        int pnode = playerR * COLS + playerC;
        int target = exitR * COLS + exitC;
        if (!chips.empty()) {                       // go for nearest chip (BFS distances)
            vector<int> d = bfsDist(graph, pnode);
            int bestD = 1000000;
            for (auto& ch : chips) {
                int id = ch.first * COLS + ch.second;
                if (d[id] < bestD) { bestD = d[id]; target = id; }
            }
        }
        hintTarget = target;
        hintTimer = 3.f;
        hintsLeft--;
    };

    auto useUndo = [&]() {
        if (undosLeft <= 0) { msg = "No undos left!"; msgTimer = 1.5f; return; }
        if (history.empty()) return;
        for (int i = 0; i < 3 && !history.empty(); i++) {   // pop up to 3 moves
            playerR = history.top().first;
            playerC = history.top().second;
            history.pop();
        }
        undosLeft--;
        msg = "Rewound 3 moves";
        msgTimer = 1.f;
        checkHit();
    };

    // ---------- shapes ----------
    sf::RectangleShape wallShape(sf::Vector2f((float)CELL, (float)CELL));
    wallShape.setFillColor(sf::Color(14, 22, 64));
    wallShape.setOutlineColor(sf::Color(0, 140, 255, 140));
    wallShape.setOutlineThickness(-1.5f);
    sf::RectangleShape floorShape(sf::Vector2f((float)CELL, (float)CELL));
    floorShape.setFillColor(sf::Color(6, 10, 26, 205));
    sf::RectangleShape box(sf::Vector2f((float)CELL, (float)CELL));   // general purpose

    sf::View view(sf::FloatRect({0.f, 0.f}, {(float)W, (float)H}));
    sf::Clock frameClock;

    // =================== MAIN LOOP ===================
    while (window.isOpen()) {
        float dt = frameClock.restart().asSeconds();
        if (dt > 0.05f) dt = 0.05f;
        totalTime += dt;

        // ---------- events ----------
        while (const optional<sf::Event> event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) window.close();
            if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                auto k = key->code;
                if (k == sf::Keyboard::Key::Escape) window.close();
                if (state == MENU || state == GAME_OVER) {
                    if (k == sf::Keyboard::Key::Enter) newGame();
                } else if (state == LEVEL_DONE) {
                    if (k == sf::Keyboard::Key::Enter) { level++; startLevel(); state = PLAYING; }
                } else if (state == PLAYING) {
                    if (k == sf::Keyboard::Key::H) useHint();
                    if (k == sf::Keyboard::Key::U) useUndo();
                    if (k == sf::Keyboard::Key::G) showGraph = !showGraph;
                }
            }
        }

        // ---------- update ----------
        if (state == PLAYING) {
            timeLeft -= dt;
            invuln = max(0.f, invuln - dt);
            msgTimer = max(0.f, msgTimer - dt);

            if (timeLeft <= 0) {
                msg = "TIME OUT! -1 life";
                msgTimer = 2.f;
                loseLife();
                timeLeft += 30.f;
            }

            // player movement (real-time keys with a small cooldown)
            moveCooldown -= dt;
            if (state == PLAYING && moveCooldown <= 0) {
                int dr = 0, dc = 0;
                using K = sf::Keyboard::Key;
                if (sf::Keyboard::isKeyPressed(K::Up)    || sf::Keyboard::isKeyPressed(K::W)) dr = -1;
                else if (sf::Keyboard::isKeyPressed(K::Down)  || sf::Keyboard::isKeyPressed(K::S)) dr = 1;
                else if (sf::Keyboard::isKeyPressed(K::Left)  || sf::Keyboard::isKeyPressed(K::A)) dc = -1;
                else if (sf::Keyboard::isKeyPressed(K::Right) || sf::Keyboard::isKeyPressed(K::D)) dc = 1;

                if (dr != 0 || dc != 0) {
                    int nr = playerR + dr, nc = playerC + dc;
                    if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && maze[nr][nc] == 0) {
                        history.push({playerR, playerC});       // STACK push
                        playerR = nr;
                        playerC = nc;
                        moveCooldown = 0.13f;

                        // collect chip?
                        for (size_t i = 0; i < chips.size(); i++)
                            if (chips[i].first == playerR && chips[i].second == playerC) {
                                chips.erase(chips.begin() + i);
                                score += 50;
                                burst((playerC + 0.5f) * CELL, (playerR + 0.5f) * CELL, sf::Color(255, 220, 0), 25);
                                if (chips.empty()) { msg = "All chips collected! EXIT UNLOCKED"; msgTimer = 2.5f; }
                                break;
                            }
                        checkHit();
                    }
                }
            }

            // enemies move on a timer, using BFS on the graph
            enemyTimer += dt;
            if (state == PLAYING && enemyTimer >= enemyInterval) {
                enemyTimer -= enemyInterval;
                int pnode = playerR * COLS + playerC;
                for (auto& e : enemies) {
                    int enode = e.r * COLS + e.c;
                    vector<int> path = bfsPath(graph, enode, pnode);
                    int chaseRange = 10 + level;
                    int next = -1;
                    if (path.size() > 1 && (int)path.size() - 1 <= chaseRange)
                        next = path[1];                           // chase the player
                    else if (!graph[enode].empty())
                        next = graph[enode][randInt(0, (int)graph[enode].size() - 1)];  // wander
                    if (next != -1) { e.r = next / COLS; e.c = next % COLS; }
                }
                checkHit();
            }

            // hint path follows the player while the hint is active
            if (hintTimer > 0) {
                hintTimer -= dt;
                hintPath = bfsPath(graph, playerR * COLS + playerC, hintTarget);
            } else hintPath.clear();

            // reached exit?
            if (state == PLAYING && playerR == exitR && playerC == exitC) {
                if (chips.empty()) {
                    lastBonus = 100 * level + (int)timeLeft * 5 + lives * 50 - (3 - hintsLeft) * 20;
                    if (lastBonus < 0) lastBonus = 0;
                    score += lastBonus;
                    burst((exitC + 0.5f) * CELL, (exitR + 0.5f) * CELL, sf::Color(0, 255, 140), 50);
                    state = LEVEL_DONE;
                } else if (msgTimer <= 0) {
                    msg = "EXIT LOCKED - collect all data chips!";
                    msgTimer = 1.5f;
                }
            }
        }

        // smooth visual positions (animation only)
        float kp = min(1.f, dt * 16.f), ke = min(1.f, dt * 12.f);
        pvr += (playerR - pvr) * kp;
        pvc += (playerC - pvc) * kp;
        for (auto& e : enemies) {
            e.vr += (e.r - e.vr) * ke;
            e.vc += (e.c - e.vc) * ke;
        }
        trail.push_back(sf::Vector2f((pvc + 0.5f) * CELL, (pvr + 0.5f) * CELL));
        if (trail.size() > 14) trail.erase(trail.begin());

        for (auto& p : particles) { p.pos += p.vel * dt; p.life -= dt; }
        particles.erase(remove_if(particles.begin(), particles.end(),
                        [](const Particle& p) { return p.life <= 0; }), particles.end());

        flash = max(0.f, flash - dt * 2.5f);
        shake = max(0.f, shake - dt);

        // screen shake via view offset
        float ox = 0, oy = 0;
        if (shake > 0) { ox = randInt(-100, 100) / 100.f * shake * 14.f; oy = randInt(-100, 100) / 100.f * shake * 14.f; }
        view.setCenter({W / 2.f + ox, H / 2.f + oy});
        window.setView(view);

        // =================== DRAW ===================
        window.clear(sf::Color(3, 5, 14));

        // matrix rain background
        sf::RectangleShape drop(sf::Vector2f(2.f, 8.f));
        for (int i = 0; i < COLS * 2; i++) {
            rainY[i] += rainSpeed[i] * dt;
            if (rainY[i] > ROWS * CELL + 90) rainY[i] = -(float)randInt(0, 200);
            for (int s = 0; s < 10; s++) {
                float y = rainY[i] - s * 10.f;
                if (y < 0 || y > ROWS * CELL) continue;
                drop.setPosition({i * CELL / 2.f + 8.f, y});
                drop.setFillColor(sf::Color(0, 255, 120, (uint8_t)(80 - s * 7)));
                window.draw(drop);
            }
        }

        if (state != MENU) {
            // maze cells
            for (int r = 0; r < ROWS; r++)
                for (int c = 0; c < COLS; c++) {
                    if (maze[r][c] == 1) { wallShape.setPosition({(float)c * CELL, (float)r * CELL}); window.draw(wallShape); }
                    else                 { floorShape.setPosition({(float)c * CELL, (float)r * CELL}); window.draw(floorShape); }
                }

            // exit
            float pulse = 0.5f + 0.5f * sin(totalTime * 4.f);
            bool open = chips.empty();
            sf::Color ec = open ? sf::Color(0, 255, 140) : sf::Color(200, 50, 60);
            glow((exitC + 0.5f) * CELL, (exitR + 0.5f) * CELL, 8.f + 5.f * pulse, ec);
            box.setSize(sf::Vector2f(CELL - 14.f, CELL - 14.f));
            box.setPosition({exitC * CELL + 7.f, exitR * CELL + 7.f});
            box.setFillColor(ec);
            window.draw(box);

            // chips (rotating diamonds)
            for (auto& ch : chips) {
                float cx = (ch.second + 0.5f) * CELL, cy = (ch.first + 0.5f) * CELL;
                glow(cx, cy, 6.f + 2.f * pulse, sf::Color(255, 220, 0));
                sf::CircleShape d(9.f, 4);
                d.setOrigin({9.f, 9.f});
                d.setPosition({cx, cy});
                d.setRotation(sf::degrees(totalTime * 90.f));
                d.setFillColor(sf::Color(255, 230, 60));
                window.draw(d);
            }

            // enemies (red bugs)
            for (auto& e : enemies) {
                float ex = (e.vc + 0.5f) * CELL, ey = (e.vr + 0.5f) * CELL;
                glow(ex, ey, 8.f + 3.f * pulse, sf::Color(255, 40, 60));
                sf::CircleShape b(11.f, 6);
                b.setOrigin({11.f, 11.f});
                b.setPosition({ex, ey});
                b.setRotation(sf::degrees(-totalTime * 120.f));
                b.setFillColor(sf::Color(255, 50, 70));
                b.setOutlineColor(sf::Color(255, 180, 190));
                b.setOutlineThickness(1.5f);
                window.draw(b);
            }

            // player trail + player
            for (size_t i = 0; i < trail.size(); i++) {
                sf::CircleShape t(2.f + i * 0.35f);
                t.setOrigin({t.getRadius(), t.getRadius()});
                t.setPosition(trail[i]);
                t.setFillColor(sf::Color(0, 255, 220, (uint8_t)(i * 9)));
                window.draw(t);
            }
            bool blink = invuln > 0 && fmod(totalTime * 10.f, 2.f) < 1.f;
            if (state == PLAYING && !blink) {
                float px = (pvc + 0.5f) * CELL, py = (pvr + 0.5f) * CELL;
                glow(px, py, 8.f + 2.f * pulse, sf::Color(0, 255, 220));
                sf::CircleShape p(11.f);
                p.setOrigin({11.f, 11.f});
                p.setPosition({px, py});
                p.setFillColor(sf::Color(200, 255, 250));
                p.setOutlineColor(sf::Color(0, 255, 220));
                p.setOutlineThickness(2.f);
                window.draw(p);
            }

            // particles
            sf::RectangleShape spark(sf::Vector2f(3.f, 3.f));
            for (auto& p : particles) {
                spark.setPosition(p.pos);
                spark.setFillColor(sf::Color(p.color.r, p.color.g, p.color.b, (uint8_t)(min(1.f, p.life) * 255)));
                window.draw(spark);
            }

            // FOG OF WAR (hidden while graph x-ray view is on)
            if (!showGraph && state == PLAYING) {
                for (int r = 0; r < ROWS; r++)
                    for (int c = 0; c < COLS; c++) {
                        float d = sqrt((r - pvr) * (r - pvr) + (c - pvc) * (c - pvc));
                        float a = clamp((d - (fogRadius - 1.5f)) * 85.f, 0.f, 245.f);
                        if (a <= 0) continue;
                        box.setSize(sf::Vector2f((float)CELL, (float)CELL));
                        box.setPosition({(float)c * CELL, (float)r * CELL});
                        box.setFillColor(sf::Color(2, 3, 10, (uint8_t)a));
                        window.draw(box);
                    }
            }

            // GRAPH x-ray view: nodes + edges
            if (showGraph) {
                sf::VertexArray lines(sf::PrimitiveType::Lines);
                for (int u = 0; u < (int)graph.size(); u++)
                    for (int v : graph[u])
                        if (v > u) {
                            sf::Vector2f a((u % COLS + 0.5f) * CELL, (u / COLS + 0.5f) * CELL);
                            sf::Vector2f b((v % COLS + 0.5f) * CELL, (v / COLS + 0.5f) * CELL);
                            lines.append(sf::Vertex{a, sf::Color(0, 180, 255, 170)});
                            lines.append(sf::Vertex{b, sf::Color(0, 180, 255, 170)});
                        }
                window.draw(lines);
                sf::CircleShape node(3.f);
                node.setOrigin({3.f, 3.f});
                node.setFillColor(sf::Color(0, 255, 200));
                for (int u = 0; u < (int)graph.size(); u++) {
                    if (graph[u].empty()) continue;
                    node.setPosition({(u % COLS + 0.5f) * CELL, (u / COLS + 0.5f) * CELL});
                    window.draw(node);
                }
            }

            // hint path (on top of fog)
            for (int id : hintPath) {
                box.setSize(sf::Vector2f(CELL - 16.f, CELL - 16.f));
                box.setPosition({(id % COLS) * CELL + 8.f, (id / COLS) * CELL + 8.f});
                box.setFillColor(sf::Color(255, 230, 0, 150));
                window.draw(box);
            }

            // danger tint when an enemy is close
            if (state == PLAYING) {
                int minD = 100;
                for (auto& e : enemies) minD = min(minD, abs(e.r - playerR) + abs(e.c - playerC));
                if (minD <= 5) {
                    float a = (6 - minD) * 9.f * (0.6f + 0.4f * sin(totalTime * 10.f));
                    box.setSize(sf::Vector2f((float)W, (float)(ROWS * CELL)));
                    box.setPosition({0.f, 0.f});
                    box.setFillColor(sf::Color(255, 0, 0, (uint8_t)a));
                    window.draw(box);
                }
            }

            // hit flash
            if (flash > 0) {
                box.setSize(sf::Vector2f((float)W, (float)H));
                box.setPosition({0.f, 0.f});
                box.setFillColor(sf::Color(255, 0, 0, (uint8_t)(flash * 140)));
                window.draw(box);
            }

            // HUD
            box.setSize(sf::Vector2f((float)W, (float)HUD_H));
            box.setPosition({0.f, (float)(ROWS * CELL)});
            box.setFillColor(sf::Color(0, 0, 0));
            window.draw(box);
            box.setSize(sf::Vector2f((float)W, 2.f));
            box.setFillColor(sf::Color(0, 200, 255));
            window.draw(box);

            float hy = ROWS * CELL + 6.f;
            sf::Color hc(0, 255, 200);
            drawText("LEVEL " + to_string(level), 10, hy, 16, hc, false);
            drawText("CHIPS " + to_string(chipsTotal - (int)chips.size()) + "/" + to_string(chipsTotal), 190, hy, 16, sf::Color(255, 230, 60), false);
            drawText("TIME " + to_string(max(0, (int)ceil(timeLeft))), 390, hy, 16,
                     timeLeft < 15 ? sf::Color(255, 70, 70) : hc, false);
            drawText("LIVES " + to_string(lives), 580, hy, 16, sf::Color(255, 90, 110), false);
            drawText("SCORE " + to_string(score), 10, hy + 22, 16, hc, false);
            drawText("HINT[H] " + to_string(hintsLeft), 190, hy + 22, 16, hc, false);
            drawText("UNDO[U] " + to_string(undosLeft), 390, hy + 22, 16, hc, false);
            drawText(showGraph ? "GRAPH[G] ON" : "GRAPH[G]", 580, hy + 22, 16, hc, false);

            if (msgTimer > 0 && state == PLAYING)
                drawText(msg, W / 2.f, 8.f, 20, sf::Color(255, 230, 60), true);
        }

        // ---------- screens ----------
        if (state == MENU) {
            drawText("CYBER ESCAPE", W / 2.f, 60.f, 56, sf::Color(0, 255, 220), true);
            drawText("ANTIVIRUS HUNT", W / 2.f, 128.f, 26, sf::Color(255, 70, 90), true);
            drawText("Collect all data chips, then reach the exit", W / 2.f, 210.f, 18, sf::Color(200, 230, 255), true);
            drawText("Red bugs hunt you using BFS. Fog hides the maze.", W / 2.f, 238.f, 18, sf::Color(200, 230, 255), true);
            drawText("Arrows/WASD move   H hint   U undo   G graph view", W / 2.f, 280.f, 18, sf::Color(0, 255, 200), true);
            if (fmod(totalTime, 1.2f) < 0.8f)
                drawText("PRESS ENTER TO START", W / 2.f, 360.f, 28, sf::Color(255, 230, 60), true);
        }
        if (state == LEVEL_DONE || state == GAME_OVER) {
            box.setSize(sf::Vector2f((float)W, (float)H));
            box.setPosition({0.f, 0.f});
            box.setFillColor(sf::Color(0, 0, 0, 175));
            window.draw(box);
            if (state == LEVEL_DONE) {
                drawText("LEVEL " + to_string(level) + " CLEARED", W / 2.f, 150.f, 44, sf::Color(0, 255, 140), true);
                drawText("Bonus +" + to_string(lastBonus) + "     Score " + to_string(score), W / 2.f, 225.f, 24, sf::Color(255, 230, 60), true);
                drawText("Next level: more bugs, less vision, less time", W / 2.f, 275.f, 18, sf::Color(200, 230, 255), true);
                drawText("PRESS ENTER", W / 2.f, 340.f, 28, sf::Color(0, 255, 220), true);
            } else {
                drawText("SYSTEM BREACHED", W / 2.f, 140.f, 48, sf::Color(255, 70, 90), true);
                drawText("Reached level " + to_string(level) + "     Score " + to_string(score), W / 2.f, 220.f, 24, sf::Color(255, 230, 60), true);
                drawText("PRESS ENTER TO TRY AGAIN", W / 2.f, 300.f, 26, sf::Color(0, 255, 220), true);
            }
        }

        window.display();
    }
    return 0;
}