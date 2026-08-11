# Playing on Lichess

ChessBot speaks UCI (`./chessbot --uci`), which is all the
[Lichess Bot API](https://lichess.org/api#tag/Bot) needs. This directory holds
the integration:

| file | what it is |
|---|---|
| `config.yml` | lichess-bot's configuration, tailored to this engine |
| `run.sh` | builds the engine, then starts lichess-bot against this config |
| `chessbot-uci.sh` (repo root) | the engine as a plain UCI binary |

**lichess-bot itself is not in this repository.** It is AGPL-3.0 and has its
own release cadence, so it is cloned separately and pointed at from here.

## One-time setup

```bash
git clone https://github.com/lichess-bot-devs/lichess-bot.git ../lichess-bot
```

`run.sh` looks for it next to this repo — `/home/dheirav/Code/lichess-bot` —
and creates the virtualenv and installs the dependencies on first use. Set
`LICHESS_BOT_DIR` to keep the checkout somewhere else.

The account needs to be a **BOT account**, which requires an account that has
never played a rated game. The upgrade is permanent, and afterwards the account
can only play through the API — not from the website.

```bash
read -rs LICHESS_BOT_TOKEN && export LICHESS_BOT_TOKEN   # token, not echoed
./lichess/run.sh -u                                      # upgrade, then play
```

To check whether an account is already a bot:

```bash
curl -s https://lichess.org/api/user/<username> | grep -o '"title":"[^"]*"'
```

`"title":"BOT"` means yes; no `title` field at all means no.

## Playing

```bash
read -rs LICHESS_BOT_TOKEN && export LICHESS_BOT_TOKEN
./lichess/run.sh
```

Under `tmux`, so that closing the terminal does not abandon a game in progress:

```bash
tmux new -s bot './lichess/run.sh'
```

The token is read from the environment rather than `config.yml`, because that
file is in version control.

## Why the config says what it says

- **`ponder: false`** — the engine has no ponder support. Asking for it stalls it.
- **No bullet, minimum 3-minute base.** The time manager divides the remaining
  clock by an assumed 30 moves and there is no pondering, so bullet is where it
  flags rather than where it plays badly.
- **`uci_options` lists only `Hash`.** python-chess raises on any option the
  engine did not advertise in its `uci` reply, so the template's `Threads`,
  `SyzygyPath` and `Move Overhead` would fail on the first game. The options
  this engine does advertise are `Hash`, `NullMove`, `LMR`, `Aspiration`,
  `SeeOrdering` and `SeePruning` — the last two are commented out in the config
  and should be enabled once their gates pass (PLAN.md 3.2).
- **`move_overhead: 2000`.** Raise it if games are lost on time despite the
  engine returning moves promptly; WSL plus network latency is what it covers.
- **Matchmaking is on and rated**, 5+3 and 10+5, within 300 rating points. Rated
  games are the point: a Lichess rating is an *independent* strength measurement,
  which self-play SPRT cannot produce — that only ever measures a change against
  the previous version of itself.

## Checking the engine end without Lichess

This drives the engine exactly the way lichess-bot does, and needs no token:

```bash
~/lichess-bot/venv/bin/python - <<'PY'
import chess, chess.engine
eng = chess.engine.SimpleEngine.popen_uci("./chessbot-uci.sh")
print(eng.id)
eng.configure({"Hash": 256})
board = chess.Board()
print(eng.play(board, chess.engine.Limit(white_clock=180, black_clock=180,
                                         white_inc=2, black_inc=2)).move)
eng.quit()
PY
```
