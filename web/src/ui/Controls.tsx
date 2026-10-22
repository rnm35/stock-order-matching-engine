import { useState } from "react";

import type { Mode } from "../engine/engine.ts";
import type { Bot } from "../sim/bots.ts";
import type { MarketStats } from "../sim/market.ts";

interface Props {
    running: boolean;
    onToggleRunning: () => void;
    speed: number;
    onSpeed: (actionsPerSecond: number) => void;
    bots: Bot[];
    onBotsChanged: () => void;
    mode: Mode;
    onReset: (mode: Mode) => void;
    onBurst: () => void;
    stats: MarketStats;
    onBenchmark: () => void;
}

// The speed slider is logarithmic: 10 to 30,000 bot actions per second.
const MIN = Math.log10(10);
const MAX = Math.log10(30_000);

export function Controls(props: Props) {
    const [nextMode, setNextMode] = useState<Mode>(props.mode);
    // Bots are mutable objects owned by the Market; re-render after toggling them.
    const [, setVersion] = useState(0);

    return (
        <div className="controls">
            <div className="row">
                <button type="button" onClick={props.onToggleRunning} aria-pressed={props.running}>
                    {props.running ? "Pause" : "Run"}
                </button>
                <button type="button" onClick={props.onBurst} title="Run 10,000 bot actions at once">
                    +10k actions
                </button>
                <button
                    type="button"
                    onClick={props.onBenchmark}
                    title="Time 100,000 engine requests in this browser (updates Engine µs/req)"
                >
                    Benchmark
                </button>
            </div>

            <label className="slider">
                Bot speed: {Math.round(props.speed).toLocaleString()} actions/s
                <input
                    type="range"
                    min={MIN}
                    max={MAX}
                    step={0.01}
                    value={Math.log10(props.speed)}
                    onChange={(e) => props.onSpeed(10 ** Number(e.target.value))}
                />
            </label>

            <fieldset>
                <legend>Traders</legend>
                {props.bots.map((bot) => (
                    <label key={bot.name} className="check">
                        <input
                            type="checkbox"
                            checked={bot.enabled}
                            onChange={(e) => {
                                bot.enabled = e.target.checked;
                                setVersion((v) => v + 1);
                                props.onBotsChanged();
                            }}
                        />
                        {bot.name}
                    </label>
                ))}
            </fieldset>

            <div className="row">
                <label>
                    Rules
                    <select value={nextMode} onChange={(e) => setNextMode(e.target.value as Mode)}>
                        <option value="modern">Modern (exchange-style)</option>
                        <option value="legacy">Legacy (coursework rules)</option>
                    </select>
                </label>
                <button type="button" onClick={() => props.onReset(nextMode)}>
                    Reset
                </button>
            </div>

            <p className="muted small-print">
                {props.stats.requests.toLocaleString()} requests · {props.stats.trades.toLocaleString()} trades ·{" "}
                {props.stats.volume.toLocaleString()} shares
            </p>
        </div>
    );
}
