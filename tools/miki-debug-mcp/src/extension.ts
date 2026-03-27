import * as vscode from 'vscode';
import { MikiDebugMcpServer } from './mcpServer.js';
import { registerOutputTracker } from './debugController.js';

let server: MikiDebugMcpServer | null = null;

export async function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('mikiDebugMcp');
    const port = config.get<number>('serverPort', 3001);

    // Register DAP output tracker to capture Debug Console messages
    registerOutputTracker(context);

    server = new MikiDebugMcpServer(port);
    await server.start();

    const channel = vscode.window.createOutputChannel('miki Debug MCP');
    channel.appendLine(`miki-debug-mcp server running on http://localhost:${port}/mcp`);

    const restartCmd = vscode.commands.registerCommand('mikiDebugMcp.restart', async () => {
        if (server) { await server.stop(); }
        server = new MikiDebugMcpServer(port);
        await server.start();
        vscode.window.showInformationMessage(`miki-debug-mcp restarted on port ${port}`);
    });

    context.subscriptions.push(restartCmd, { dispose: () => { server?.stop(); } });
}

export async function deactivate() {
    if (server) { await server.stop(); server = null; }
}
