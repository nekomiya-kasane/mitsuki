import * as vscode from 'vscode';

// ---------------------------------------------------------------------------
// Debug Console output tracker
// ---------------------------------------------------------------------------

const MAX_CONSOLE_LINES = 500;
let consoleOutput: { timestamp: number; category: string; text: string }[] = [];
let outputDisposable: vscode.Disposable | null = null;

/** Start capturing debug console output. Call once at extension activation. */
export function startConsoleCapture(): void {
    if (outputDisposable) return;
    outputDisposable = vscode.debug.onDidReceiveDebugSessionCustomEvent(() => { /* noop — we use the tracker below */ });
    // The real capture comes from debug.registerDebugAdapterTrackerFactory
}

/** Register a DebugAdapterTrackerFactory that captures stdout/stderr/console messages. */
export function registerOutputTracker(context: vscode.ExtensionContext): void {
    const factory: vscode.DebugAdapterTrackerFactory = {
        createDebugAdapterTracker(_session: vscode.DebugSession): vscode.ProviderResult<vscode.DebugAdapterTracker> {
            return {
                onDidSendMessage(message: any) {
                    if (message.type === 'event' && message.event === 'output') {
                        const body = message.body;
                        if (body?.output) {
                            consoleOutput.push({
                                timestamp: Date.now(),
                                category: body.category ?? 'console',
                                text: body.output,
                            });
                            // Ring buffer
                            if (consoleOutput.length > MAX_CONSOLE_LINES) {
                                consoleOutput = consoleOutput.slice(-MAX_CONSOLE_LINES);
                            }
                        }
                    }
                },
            };
        },
    };
    context.subscriptions.push(vscode.debug.registerDebugAdapterTrackerFactory('*', factory));
}

/** Compact representation of debug state returned to MCP clients. */
export interface DebugSnapshot {
    sessionActive: boolean;
    configName: string | null;
    file: string | null;
    line: number | null;
    currentLineText: string | null;
    frameName: string | null;
    nextLines: string[];
    breakpoints: string[];
    stackTrace: { name: string; file?: string; line?: number }[];
}

// ---------------------------------------------------------------------------
// Session control
// ---------------------------------------------------------------------------

export async function listLaunchConfigs(): Promise<string> {
    const folders = vscode.workspace.workspaceFolders;
    if (!folders || folders.length === 0) return JSON.stringify({ configs: [] });

    const configs: { name: string; type: string; request: string }[] = [];
    for (const folder of folders) {
        const launch = vscode.workspace.getConfiguration('launch', folder.uri);
        const cfgs = launch.get<any[]>('configurations', []);
        for (const c of cfgs) {
            configs.push({ name: c.name, type: c.type, request: c.request });
        }
    }
    return JSON.stringify({ configs }, null, 2);
}

export async function startDebugSession(configName?: string): Promise<string> {
    // If already debugging, report it
    if (vscode.debug.activeDebugSession) {
        return `Already debugging: "${vscode.debug.activeDebugSession.name}". Stop it first or use restart.`;
    }

    const folders = vscode.workspace.workspaceFolders;
    if (!folders || folders.length === 0) return 'No workspace folder open.';

    let config: vscode.DebugConfiguration | undefined;
    let folder: vscode.WorkspaceFolder = folders[0];

    if (configName) {
        for (const f of folders) {
            const launch = vscode.workspace.getConfiguration('launch', f.uri);
            const cfgs = launch.get<any[]>('configurations', []);
            const match = cfgs.find((c: any) => c.name === configName);
            if (match) { config = match; folder = f; break; }
        }
        if (!config) return `Launch configuration "${configName}" not found.`;
    } else {
        // Pick the first configuration
        const launch = vscode.workspace.getConfiguration('launch', folder.uri);
        const cfgs = launch.get<any[]>('configurations', []);
        if (cfgs.length === 0) return 'No launch configurations found in launch.json.';
        config = cfgs[0];
    }

    const started = await vscode.debug.startDebugging(folder, config!);
    if (!started) return 'vscode.debug.startDebugging returned false — session failed to start.';

    // Wait for session to become ready (up to 30s)
    const ready = await waitForActiveSession(30_000);
    if (!ready) return 'Debug session started but did not become active within 30 seconds.';

    const snap = await getSnapshot();
    return JSON.stringify({ status: 'started', ...snap }, null, 2);
}

export async function stopDebugSession(): Promise<string> {
    const session = vscode.debug.activeDebugSession;
    if (!session) return 'No active debug session.';
    await vscode.debug.stopDebugging(session);
    return 'Debug session stopped.';
}

export async function restartDebugSession(): Promise<string> {
    if (!vscode.debug.activeDebugSession) return 'No active debug session to restart.';
    await vscode.commands.executeCommand('workbench.action.debug.restart');
    await delay(500);
    return 'Debug session restarted.';
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

async function stepCommand(cmd: string, label: string): Promise<string> {
    if (!vscode.debug.activeDebugSession) return 'No active debug session.';
    const before = await getSnapshot();
    await vscode.commands.executeCommand(cmd);
    const after = await waitForStateChange(before, 30_000);
    return JSON.stringify(after, null, 2);
}

export const stepOver = () => stepCommand('workbench.action.debug.stepOver', 'step_over');
export const stepInto = () => stepCommand('workbench.action.debug.stepInto', 'step_into');
export const stepOut  = () => stepCommand('workbench.action.debug.stepOut',  'step_out');
export const continueExec = () => stepCommand('workbench.action.debug.continue', 'continue');

// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------

export async function addBreakpoint(filePath: string, line: number): Promise<string> {
    const uri = vscode.Uri.file(filePath);
    const bp = new vscode.SourceBreakpoint(new vscode.Location(uri, new vscode.Position(line - 1, 0)));
    vscode.debug.addBreakpoints([bp]);
    return `Breakpoint added at ${filePath}:${line}`;
}

export async function removeBreakpoint(filePath: string, line: number): Promise<string> {
    const uri = vscode.Uri.file(filePath);
    const matching = vscode.debug.breakpoints.filter(bp =>
        bp instanceof vscode.SourceBreakpoint &&
        bp.location.uri.toString() === uri.toString() &&
        bp.location.range.start.line === line - 1
    );
    if (matching.length === 0) return `No breakpoint found at ${filePath}:${line}`;
    vscode.debug.removeBreakpoints(matching);
    return `Removed ${matching.length} breakpoint(s) at ${filePath}:${line}`;
}

export async function clearAllBreakpoints(): Promise<string> {
    const count = vscode.debug.breakpoints.length;
    if (count === 0) return 'No breakpoints to clear.';
    vscode.debug.removeBreakpoints([...vscode.debug.breakpoints]);
    return `Cleared ${count} breakpoint(s).`;
}

export async function listBreakpoints(): Promise<string> {
    const bps = vscode.debug.breakpoints
        .filter((bp): bp is vscode.SourceBreakpoint => bp instanceof vscode.SourceBreakpoint)
        .map(bp => ({
            file: bp.location.uri.fsPath,
            line: bp.location.range.start.line + 1,
            enabled: bp.enabled,
        }));
    return JSON.stringify({ breakpoints: bps }, null, 2);
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

export async function getVariables(scope: 'local' | 'global' | 'all' = 'all'): Promise<string> {
    const session = vscode.debug.activeDebugSession;
    if (!session) return 'No active debug session.';

    const stackItem = vscode.debug.activeStackItem;
    if (!stackItem || !('frameId' in stackItem)) return 'No active stack frame.';

    const scopesResp = await session.customRequest('scopes', { frameId: (stackItem as any).frameId });
    if (!scopesResp?.scopes) return 'No scopes available.';

    const filtered = scopesResp.scopes.filter((s: any) => {
        if (scope === 'all') return true;
        const n = s.name.toLowerCase();
        return scope === 'local' ? n.includes('local') : n.includes('global');
    });

    const result: any[] = [];
    for (const s of filtered) {
        try {
            const vars = await session.customRequest('variables', { variablesReference: s.variablesReference });
            result.push({ scope: s.name, variables: (vars.variables || []).map((v: any) => ({ name: v.name, value: v.value, type: v.type })) });
        } catch {
            result.push({ scope: s.name, error: 'Failed to retrieve variables' });
        }
    }
    return JSON.stringify({ scopes: result }, null, 2);
}

export async function evaluateExpression(expression: string): Promise<string> {
    const session = vscode.debug.activeDebugSession;
    if (!session) return 'No active debug session.';

    const stackItem = vscode.debug.activeStackItem;
    if (!stackItem || !('frameId' in stackItem)) return 'No active stack frame.';

    const resp = await session.customRequest('evaluate', {
        expression,
        frameId: (stackItem as any).frameId,
        context: 'repl',
    });
    return JSON.stringify({ expression, result: resp.result, type: resp.type }, null, 2);
}

export async function getDiagnostics(): Promise<string> {
    const all = vscode.languages.getDiagnostics();
    const entries: { file: string; severity: string; line: number; message: string }[] = [];
    const severityMap = ['Error', 'Warning', 'Information', 'Hint'];
    for (const [uri, diags] of all) {
        for (const d of diags) {
            entries.push({
                file: uri.fsPath,
                severity: severityMap[d.severity] ?? 'Unknown',
                line: d.range.start.line + 1,
                message: d.message,
            });
        }
    }
    return JSON.stringify({ count: entries.length, diagnostics: entries }, null, 2);
}

// ---------------------------------------------------------------------------
// Debug Console CRUD
// ---------------------------------------------------------------------------

export async function debugConsoleExecute(command: string, context: 'repl' | 'watch' = 'repl'): Promise<string> {
    const session = vscode.debug.activeDebugSession;
    if (!session) return 'No active debug session.';

    const stackItem = vscode.debug.activeStackItem;
    const frameId = (stackItem && 'frameId' in stackItem) ? (stackItem as any).frameId : undefined;

    try {
        const resp = await session.customRequest('evaluate', {
            expression: command,
            frameId,
            context,
        });
        return JSON.stringify({
            command,
            result: resp.result ?? null,
            type: resp.type ?? null,
            variablesReference: resp.variablesReference ?? 0,
        }, null, 2);
    } catch (err: any) {
        return JSON.stringify({ command, error: err?.message ?? String(err) }, null, 2);
    }
}

export async function debugConsoleRead(lastN?: number, category?: string): Promise<string> {
    let entries = consoleOutput;
    if (category) {
        entries = entries.filter(e => e.category === category);
    }
    if (lastN !== undefined && lastN > 0) {
        entries = entries.slice(-lastN);
    }
    return JSON.stringify({ count: entries.length, output: entries }, null, 2);
}

export async function debugConsoleClear(): Promise<string> {
    // Clear VS Code's debug console UI
    await vscode.commands.executeCommand('workbench.debug.panel.action.clearReplAction');
    // Clear our internal buffer
    const count = consoleOutput.length;
    consoleOutput = [];
    return `Debug console cleared (${count} captured lines flushed).`;
}

export async function setVariable(variableName: string, newValue: string, scope: 'local' | 'global' = 'local'): Promise<string> {
    const session = vscode.debug.activeDebugSession;
    if (!session) return 'No active debug session.';

    const stackItem = vscode.debug.activeStackItem;
    if (!stackItem || !('frameId' in stackItem)) return 'No active stack frame.';

    const frameId = (stackItem as any).frameId;

    // First try setExpression (DAP 2023+, supported by lldb-dap)
    try {
        const resp = await session.customRequest('setExpression', {
            expression: variableName,
            value: newValue,
            frameId,
        });
        return JSON.stringify({
            variable: variableName,
            newValue: resp.value ?? newValue,
            type: resp.type ?? null,
            method: 'setExpression',
        }, null, 2);
    } catch { /* fall through to setVariable */ }

    // Fallback: find variablesReference via scopes, then setVariable
    try {
        const scopesResp = await session.customRequest('scopes', { frameId });
        if (!scopesResp?.scopes) return 'No scopes available to find variable.';

        const targetScopes = scopesResp.scopes.filter((s: any) => {
            const n = s.name.toLowerCase();
            if (scope === 'local') return n.includes('local');
            if (scope === 'global') return n.includes('global');
            return true;
        });

        for (const s of targetScopes) {
            const varsResp = await session.customRequest('variables', {
                variablesReference: s.variablesReference,
            });
            const found = (varsResp.variables || []).find((v: any) => v.name === variableName);
            if (found) {
                const setResp = await session.customRequest('setVariable', {
                    variablesReference: s.variablesReference,
                    name: variableName,
                    value: newValue,
                });
                return JSON.stringify({
                    variable: variableName,
                    newValue: setResp.value ?? newValue,
                    type: setResp.type ?? found.type ?? null,
                    method: 'setVariable',
                }, null, 2);
            }
        }
        return `Variable "${variableName}" not found in ${scope} scope.`;
    } catch (err: any) {
        return JSON.stringify({ error: err?.message ?? String(err) }, null, 2);
    }
}

// ---------------------------------------------------------------------------
// Snapshot & helpers
// ---------------------------------------------------------------------------

export async function getSnapshot(): Promise<DebugSnapshot> {
    const snap: DebugSnapshot = {
        sessionActive: false, configName: null, file: null, line: null,
        currentLineText: null, frameName: null, nextLines: [], breakpoints: [], stackTrace: [],
    };

    const session = vscode.debug.activeDebugSession;
    if (!session) return snap;
    snap.sessionActive = true;
    snap.configName = session.configuration.name ?? null;

    const stackItem = vscode.debug.activeStackItem;
    if (stackItem && 'frameId' in stackItem) {
        try {
            const stResp = await session.customRequest('stackTrace', {
                threadId: (stackItem as any).threadId, startFrame: 0, levels: 20,
            });
            if (stResp?.stackFrames?.length) {
                snap.frameName = stResp.stackFrames[0].name ?? null;
                snap.stackTrace = stResp.stackFrames.map((f: any) => ({
                    name: f.name, file: f.source?.path, line: f.line,
                }));
            }
        } catch { /* ignore */ }
    }

    const editor = vscode.window.activeTextEditor;
    if (editor) {
        snap.file = editor.document.fileName;
        snap.line = editor.selection.active.line + 1;
        snap.currentLineText = editor.document.lineAt(editor.selection.active.line).text.trim();
        const startLine = editor.selection.active.line + 1;
        for (let i = startLine; i < Math.min(startLine + 3, editor.document.lineCount); i++) {
            const t = editor.document.lineAt(i).text.trim();
            if (t) snap.nextLines.push(t);
        }
    }

    snap.breakpoints = vscode.debug.breakpoints
        .filter((bp): bp is vscode.SourceBreakpoint => bp instanceof vscode.SourceBreakpoint)
        .map(bp => `${bp.location.uri.fsPath.split(/[/\\]/).pop()}:${bp.location.range.start.line + 1}`);

    return snap;
}

async function waitForActiveSession(timeoutMs: number): Promise<boolean> {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        if (vscode.debug.activeDebugSession) return true;
        await delay(300);
    }
    return false;
}

async function waitForStateChange(before: DebugSnapshot, timeoutMs: number): Promise<DebugSnapshot> {
    const start = Date.now();
    while (Date.now() - start < timeoutMs) {
        await delay(300);
        const after = await getSnapshot();
        if (!after.sessionActive) return after;
        if (after.file !== before.file || after.line !== before.line || after.frameName !== before.frameName) return after;
    }
    return getSnapshot();
}

function delay(ms: number): Promise<void> { return new Promise(r => setTimeout(r, ms)); }
