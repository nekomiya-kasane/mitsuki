import * as http from 'node:http';
import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StreamableHTTPServerTransport } from '@modelcontextprotocol/sdk/server/streamableHttp.js';
import { z } from 'zod';
import * as ctrl from './debugController.js';

export class MikiDebugMcpServer {
    private httpServer: http.Server | null = null;
    private mcpServer: McpServer | null = null;
    private port: number;

    constructor(port: number) {
        this.port = port;
    }

    async start(): Promise<void> {
        this.mcpServer = new McpServer({ name: 'miki-debug-mcp', version: '0.1.0' });
        this.registerTools();
        this.registerResources();

        // Dynamic import of express (ESM)
        const expressModule = await import('express');
        const express = expressModule.default;
        const app = express();
        app.use(express.json());

        app.post('/mcp', async (req: any, res: any) => {
            const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined });
            res.on('close', () => transport.close());
            await this.mcpServer!.connect(transport);
            await transport.handleRequest(req, res, req.body);
        });

        // Health check
        app.get('/health', (_req: any, res: any) => res.json({ status: 'ok' }));

        await new Promise<void>((resolve, reject) => {
            this.httpServer = app.listen(this.port, () => resolve());
            this.httpServer!.on('error', reject);
        });
    }

    async stop(): Promise<void> {
        if (this.httpServer) {
            await new Promise<void>(r => this.httpServer!.close(() => r()));
            this.httpServer = null;
        }
    }

    // -----------------------------------------------------------------------
    // Tools
    // -----------------------------------------------------------------------
    private registerTools(): void {
        const srv = this.mcpServer!;

        // --- Session control ---

        srv.registerTool('list_launch_configs', {
            title: 'List Launch Configurations',
            description: 'List all debug launch configurations from .vscode/launch.json. '
                + 'Returns name, type, and request for each configuration.',
            annotations: { readOnlyHint: true },
        }, async () => {
            return text(await ctrl.listLaunchConfigs());
        });

        srv.registerTool('start_debug_session', {
            title: 'Start Debug Session',
            description: 'Start a debug session using a named launch configuration from launch.json. '
                + 'Call list_launch_configs first to see available configs. '
                + 'If configName is omitted, the first configuration is used.',
            inputSchema: {
                configName: z.string().optional().describe('Name of the launch configuration to use'),
            },
        }, async (args: { configName?: string }) => {
            return text(await ctrl.startDebugSession(args.configName));
        });

        srv.registerTool('stop_debug_session', {
            title: 'Stop Debug Session',
            description: 'Stop the currently active debug session.',
        }, async () => {
            return text(await ctrl.stopDebugSession());
        });

        srv.registerTool('restart_debug_session', {
            title: 'Restart Debug Session',
            description: 'Restart the currently active debug session with the same configuration.',
        }, async () => {
            return text(await ctrl.restartDebugSession());
        });

        // --- Stepping ---

        srv.registerTool('step_over', {
            title: 'Step Over',
            description: 'Execute the current line and move to the next, stepping over function calls. '
                + 'Returns the new debug state (file, line, frame, stack trace).',
        }, async () => {
            return text(await ctrl.stepOver());
        });

        srv.registerTool('step_into', {
            title: 'Step Into',
            description: 'Step into the function call on the current line. '
                + 'Returns the new debug state.',
        }, async () => {
            return text(await ctrl.stepInto());
        });

        srv.registerTool('step_out', {
            title: 'Step Out',
            description: 'Step out of the current function, returning to the caller. '
                + 'Returns the new debug state.',
        }, async () => {
            return text(await ctrl.stepOut());
        });

        srv.registerTool('continue_execution', {
            title: 'Continue Execution',
            description: 'Continue execution until the next breakpoint or program exit. '
                + 'Returns the new debug state.',
        }, async () => {
            return text(await ctrl.continueExec());
        });

        // --- Breakpoints ---

        srv.registerTool('add_breakpoint', {
            title: 'Add Breakpoint',
            description: 'Add a source breakpoint at the specified file and line number.',
            inputSchema: {
                filePath: z.string().describe('Absolute path to the source file'),
                line: z.number().describe('1-based line number'),
            },
        }, async (args: { filePath: string; line: number }) => {
            return text(await ctrl.addBreakpoint(args.filePath, args.line));
        });

        srv.registerTool('remove_breakpoint', {
            title: 'Remove Breakpoint',
            description: 'Remove a breakpoint at the specified file and line number.',
            inputSchema: {
                filePath: z.string().describe('Absolute path to the source file'),
                line: z.number().describe('1-based line number'),
            },
        }, async (args: { filePath: string; line: number }) => {
            return text(await ctrl.removeBreakpoint(args.filePath, args.line));
        });

        srv.registerTool('clear_all_breakpoints', {
            title: 'Clear All Breakpoints',
            description: 'Remove all breakpoints across all files.',
        }, async () => {
            return text(await ctrl.clearAllBreakpoints());
        });

        srv.registerTool('list_breakpoints', {
            title: 'List Breakpoints',
            description: 'List all currently set breakpoints with file, line, and enabled state.',
            annotations: { readOnlyHint: true },
        }, async () => {
            return text(await ctrl.listBreakpoints());
        });

        // --- Inspection ---

        srv.registerTool('get_variables', {
            title: 'Get Variables',
            description: 'Get variable values at the current execution point. '
                + 'Requires execution to be paused (breakpoint or step).',
            inputSchema: {
                scope: z.enum(['local', 'global', 'all']).optional()
                    .describe("Variable scope filter: 'local', 'global', or 'all' (default: 'all')"),
            },
            annotations: { readOnlyHint: true },
        }, async (args: { scope?: 'local' | 'global' | 'all' }) => {
            return text(await ctrl.getVariables(args.scope));
        });

        srv.registerTool('evaluate_expression', {
            title: 'Evaluate Expression',
            description: 'Evaluate an expression in the current debug context (REPL). '
                + 'Requires execution to be paused.',
            inputSchema: {
                expression: z.string().describe('Expression to evaluate (C++, Python, etc.)'),
            },
        }, async (args: { expression: string }) => {
            return text(await ctrl.evaluateExpression(args.expression));
        });

        srv.registerTool('get_debug_state', {
            title: 'Get Debug State',
            description: 'Get a snapshot of the current debug state: session status, current file/line, '
                + 'frame name, stack trace, and breakpoints.',
            annotations: { readOnlyHint: true },
        }, async () => {
            const snap = await ctrl.getSnapshot();
            return text(JSON.stringify(snap, null, 2));
        });

        // --- Debug Console CRUD ---

        srv.registerTool('debug_console_execute', {
            title: 'Debug Console Execute',
            description: 'Execute a command or expression in the Debug Console (REPL). '
                + 'Can run LLDB commands (e.g. "p myVar", "bt", "memory read ...") '
                + 'or evaluate language expressions. Returns the result.',
            inputSchema: {
                command: z.string().describe('Command or expression to execute in the debug console'),
                context: z.enum(['repl', 'watch']).optional()
                    .describe("Evaluation context: 'repl' (default) or 'watch'"),
            },
        }, async (args: { command: string; context?: 'repl' | 'watch' }) => {
            return text(await ctrl.debugConsoleExecute(args.command, args.context));
        });

        srv.registerTool('debug_console_read', {
            title: 'Debug Console Read',
            description: 'Read captured Debug Console output (stdout, stderr, console messages). '
                + 'Optionally filter by category and limit to last N entries. '
                + 'Categories: stdout, stderr, console, telemetry.',
            inputSchema: {
                lastN: z.number().optional().describe('Return only the last N output entries'),
                category: z.string().optional().describe("Filter by output category: 'stdout', 'stderr', 'console'"),
            },
            annotations: { readOnlyHint: true },
        }, async (args: { lastN?: number; category?: string }) => {
            return text(await ctrl.debugConsoleRead(args.lastN, args.category));
        });

        srv.registerTool('debug_console_clear', {
            title: 'Debug Console Clear',
            description: 'Clear the Debug Console output (both UI and internal capture buffer).',
        }, async () => {
            return text(await ctrl.debugConsoleClear());
        });

        srv.registerTool('set_variable', {
            title: 'Set Variable',
            description: 'Modify a variable value in the current debug frame. '
                + 'Tries setExpression first (lldb-dap), falls back to setVariable (DAP). '
                + 'Requires execution to be paused.',
            inputSchema: {
                variableName: z.string().describe('Name of the variable to modify'),
                newValue: z.string().describe('New value as a string (will be parsed by the debugger)'),
                scope: z.enum(['local', 'global']).optional()
                    .describe("Scope to search in: 'local' (default) or 'global'"),
            },
        }, async (args: { variableName: string; newValue: string; scope?: 'local' | 'global' }) => {
            return text(await ctrl.setVariable(args.variableName, args.newValue, args.scope));
        });

        // --- Diagnostics ---

        srv.registerTool('get_diagnostics', {
            title: 'Get Diagnostics',
            description: 'Get all diagnostics (errors, warnings) from the VS Code Problems panel. '
                + 'Useful for finding compilation errors before starting a debug session.',
            annotations: { readOnlyHint: true },
        }, async () => {
            return text(await ctrl.getDiagnostics());
        });
    }

    // -----------------------------------------------------------------------
    // Resources
    // -----------------------------------------------------------------------
    private registerResources(): void {
        this.mcpServer!.registerResource(
            'Debug State',
            'miki-debug://state',
            { description: 'Current debug session state snapshot', mimeType: 'application/json' },
            async (uri: URL) => ({
                contents: [{
                    uri: uri.href,
                    mimeType: 'application/json',
                    text: JSON.stringify(await ctrl.getSnapshot(), null, 2),
                }],
            }),
        );
    }
}

function text(t: string) {
    return { content: [{ type: 'text' as const, text: t }] };
}
