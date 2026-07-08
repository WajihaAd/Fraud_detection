#l: pip install streamlit pandas networkx matplotlib seaborn
import plotly.graph_objects as go
import streamlit as st
import pandas as pd
import networkx as nx
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.lines as mlines
import os
import subprocess
import tempfile
import shutil
from io import StringIO
import numpy as np  

# ─────────────────────────────────────────────────────────────────────────────
# Page config
# ─────────────────────────────────────────────────────────────────────────────
st.set_page_config(
    page_title="🏦 Fraud Detection System",
    page_icon="🔍",
    layout="wide",
    initial_sidebar_state="expanded"
)

# ─────────────────────────────────────────────────────────────────────────────
# Custom CSS
# ─────────────────────────────────────────────────────────────────────────────
st.markdown("""
<style>
    .main { background-color: #0d1117; }
    .stApp { background-color: #0d1117; color: #e6edf3; }
    .metric-card {
        background: linear-gradient(135deg, #161b22, #21262d);
        border: 1px solid #30363d;
        border-radius: 12px;
        padding: 20px;
        text-align: center;
    }
    .fraud-alert {
        background: linear-gradient(135deg, #3d1a1a, #5c2020);
        border: 1px solid #f85149;
        border-radius: 8px;
        padding: 12px 16px;
        margin: 6px 0;
        color: #ff7b72;
        font-family: monospace;
    }
    .safe-badge {
        background: #1a3d1a;
        border: 1px solid #3fb950;
        color: #3fb950;
        border-radius: 4px;
        padding: 2px 8px;
        font-size: 0.8em;
    }
    .danger-badge {
        background: #3d1a1a;
        border: 1px solid #f85149;
        color: #f85149;
        border-radius: 4px;
        padding: 2px 8px;
        font-size: 0.8em;
    }
    .section-header {
        color: #58a6ff;
        border-bottom: 2px solid #21262d;
        padding-bottom: 8px;
        margin-top: 24px;
    }
    div[data-testid="stMetric"] {
        background: #161b22;
        border: 1px solid #30363d;
        border-radius: 10px;
        padding: 16px;
    }
</style>
""", unsafe_allow_html=True)

# ─────────────────────────────────────────────────────────────────────────────
# Sidebar
# ─────────────────────────────────────────────────────────────────────────────
with st.sidebar:
    st.markdown("## 🏦 Fraud Detection")
    st.markdown("**Banking Transaction Analyzer**")
    st.markdown("---")
    st.markdown("### 📂 Data Source")

    use_sample = st.checkbox("Use sample transactions", value=True)
    uploaded_file = None
    if not use_sample:
        uploaded_file = st.file_uploader(
            "Upload CSV file",
            type=["csv"],
            help="Format: tx_id, from_account, to_account, amount, timestamp"
        )

    st.markdown("---")
    st.markdown("### ⚙️ Detection Settings")
    max_graph_nodes = st.slider("Max nodes to visualize", 20, 100, 60)
    show_labels = st.checkbox("Show account labels", value=True)
    highlight_cycles = st.checkbox("Highlight fraud cycles", value=True)

    st.markdown("---")
    st.markdown("### ℹ️ About")
    st.markdown("""
    **Algorithms:**
    - 🔵 BFS – Money flow tracing
    - 🟣 DFS – Cycle detection
    - 🟢 Connected components
    
    **Fraud Patterns:**
    - Circular transactions (round-tripping)
    - High-velocity smurfing
    - Layering networks (AML)
    """)

# ─────────────────────────────────────────────────────────────────────────────
# Helper: Run C++ binary (optional, falls back to pure Python analysis)
# ─────────────────────────────────────────────────────────────────────────────
def run_cpp_detector(csv_path: str) -> bool:
    """Try to run the compiled C++ fraud detector."""
    binary = "./fraud_detector"
    if not os.path.exists(binary):
        return False
    try:
        result = subprocess.run(
            [binary, csv_path],
            capture_output=True, text=True, timeout=30
        )
        return result.returncode == 0
    except Exception:
        return False

# ─────────────────────────────────────────────────────────────────────────────
# Helper: Pure-Python fraud analysis
# ─────────────────────────────────────────────────────────────────────────────
def analyze_transactions_python(df: pd.DataFrame):
    adjacency = {}
    node_stats = {}

    for _, row in df.iterrows():
        src, dst, amt = str(row['from_account']), str(row['to_account']), float(row['amount'])
        tx_id = str(row['tx_id'])

        if src not in adjacency: adjacency[src] = []
        if dst not in adjacency: adjacency[dst] = []
        adjacency[src].append({'to': dst, 'amount': amt, 'tx_id': tx_id})

        if src not in node_stats:
            node_stats[src] = {'sent': 0, 'received': 0, 'count': 0, 'flagged': False, 'reason': ''}
        if dst not in node_stats:
            node_stats[dst] = {'sent': 0, 'received': 0, 'count': 0, 'flagged': False, 'reason': ''}

        node_stats[src]['sent'] += amt
        node_stats[src]['count'] += 1
        node_stats[dst]['received'] += amt
        node_stats[dst]['count'] += 1

    total_tx = len(df)
    avg_amount = df['amount'].mean()
    velocity_threshold = max(3, total_tx // max(1, len(node_stats)))

    fraud_cycles = []
    seen_cycle_keys = set()

    def dfs_cycle(start):
        visited, rec_stack, parent = set(), set(), {}
        def dfs(node):
            visited.add(node)
            rec_stack.add(node)
            for edge in adjacency.get(node, []):
                nbr = edge['to']
                if nbr not in visited:
                    parent[nbr] = node
                    if dfs(nbr): return True
                elif nbr in rec_stack:
                    path = [node]
                    curr = node
                    while curr != nbr and curr in parent:
                        curr = parent[curr]
                        path.append(curr)
                    path.append(nbr)
                    path.reverse()
                    key = "|".join(sorted(path))
                    if key not in seen_cycle_keys:
                        seen_cycle_keys.add(key)
                        fraud_cycles.append(path)
                    return True
            rec_stack.discard(node)
            return False
        return dfs(start)

    for account in list(adjacency.keys()):
        dfs_cycle(account)

    cycle_nodes = set(n for cycle in fraud_cycles for n in cycle)

    for acc, stats in node_stats.items():
        outgoing = adjacency.get(acc, [])
        unique_recipients = len(set(e['to'] for e in outgoing))
        max_single = max((e['amount'] for e in outgoing), default=0)

        if acc in cycle_nodes:
            stats['flagged'] = True
            stats['reason'] = 'fraud_cycle'
        elif len(outgoing) > velocity_threshold or unique_recipients > 3:
            stats['flagged'] = True
            stats['reason'] = 'high_velocity'
        elif max_single > 3 * avg_amount:
            stats['flagged'] = True
            stats['reason'] = 'amount_anomaly'

    accounts_df = pd.DataFrame([
        {
            'account_id': acc,
            'total_sent': stats['sent'],
            'total_received': stats['received'],
            'tx_count': stats['count'],
            'is_flagged': 1 if stats['flagged'] else 0,
            'flag_reason': stats['reason']
        }
        for acc, stats in node_stats.items()
    ])

    edges_df = df.copy()
    edges_df['from_account'] = edges_df['from_account'].astype(str)
    edges_df['to_account'] = edges_df['to_account'].astype(str)
    flagged_set = set(accounts_df[accounts_df['is_flagged'] == 1]['account_id'])
    edges_df['src_flagged'] = edges_df['from_account'].isin(flagged_set).astype(int)
    edges_df['dst_flagged'] = edges_df['to_account'].isin(flagged_set).astype(int)

    return accounts_df, edges_df, fraud_cycles

# ─────────────────────────────────────────────────────────────────────────────
# Load data
# ─────────────────────────────────────────────────────────────────────────────
@st.cache_data
def load_data(csv_content: str):
    return pd.read_csv(StringIO(csv_content))

if use_sample or uploaded_file is None:
    sample_path = "sample_transactions.csv"
    if not os.path.exists(sample_path):
        st.error("sample_transactions.csv not found. Please upload a file.")
        st.stop()
    with open(sample_path, 'r') as f:
        csv_content = f.read()
else:
    csv_content = uploaded_file.read().decode('utf-8')

try:
    df = load_data(csv_content)
    df.columns = [c.strip() for c in df.columns]
    df['amount'] = pd.to_numeric(df['amount'], errors='coerce').fillna(0)
    df = df[df['amount'] > 0]
except Exception as e:
    st.error(f"Failed to parse CSV: {e}")
    st.stop()

# ─────────────────────────────────────────────────────────────────────────────
# Run Analysis
# ─────────────────────────────────────────────────────────────────────────────
cpp_success = False
if use_sample:
    cpp_success = run_cpp_detector("sample_transactions.csv")

if cpp_success and os.path.exists("fraud_accounts.csv") and os.path.exists("fraud_edges.csv"):
    accounts_df = pd.read_csv("fraud_accounts.csv")
    edges_df    = pd.read_csv("fraud_edges.csv")
    fraud_cycles = []
else:
    accounts_df, edges_df, fraud_cycles = analyze_transactions_python(df)

# ─────────────────────────────────────────────────────────────────────────────
# Header
# ─────────────────────────────────────────────────────────────────────────────
st.markdown("# 🏦 Banking Fraud Detection System")
st.markdown("*Real-time transaction graph analysis using BFS / DFS algorithms*")
st.markdown("---")

# ─────────────────────────────────────────────────────────────────────────────
# KPI Metrics
# ─────────────────────────────────────────────────────────────────────────────
col1, col2, col3, col4, col5 = st.columns(5)

total_tx       = len(df)
total_accounts = len(accounts_df)
flagged        = int(accounts_df['is_flagged'].sum())
fraud_pct      = round(flagged / total_accounts * 100, 1) if total_accounts > 0 else 0
total_vol      = df['amount'].sum()

with col1:
    st.metric("📊 Total Transactions", f"{total_tx:,}")
with col2:
    st.metric("👤 Total Accounts", f"{total_accounts:,}")
with col3:
    st.metric("🚨 Suspicious Accounts", f"{flagged:,}",
              delta=f"{fraud_pct}% flagged", delta_color="inverse")
with col4:
    st.metric("🔄 Fraud Cycles", f"{len(fraud_cycles):,}")
with col5:
    st.metric("💰 Total Volume", f"${total_vol:,.0f}")

st.markdown("---")

# ─────────────────────────────────────────────────────────────────────────────
# EDGE COLOR MAP
# ─────────────────────────────────────────────────────────────────────────────
NORMAL_BLUE = '#1f77ff'

EDGE_COLOR_MAP = {
    'fraud_cycle':   '#FFFFFF',
    'high_velocity': '#FF69B4',
    'amount_anomaly':'#FF00FF',
    'suspicious':    '#00FF00',
    'normal':        NORMAL_BLUE,
}

NODE_COLOR_FLAGGED = '#FF1744'
NODE_COLOR_NORMAL  = NORMAL_BLUE

# ─────────────────────────────────────────────────────────────────────────────
# Graph + Fraud Alerts layout
# ─────────────────────────────────────────────────────────────────────────────
col_graph, col_alerts = st.columns([2, 1])

with col_graph:
    st.markdown("### 🕸️ Transaction Graph")
    st.caption("Red nodes = suspicious | Edge colour = fraud type")

    # Build fast reason lookup: account_id -> flag_reason string
    reason_lookup = dict(
        zip(
            accounts_df['account_id'].astype(str),
            accounts_df['flag_reason'].fillna('normal').astype(str)
        )
    )

    flagged_set = set(accounts_df[accounts_df['is_flagged'] == 1]['account_id'].astype(str))

    # Build directed graph
    G = nx.DiGraph()
    for _, row in accounts_df.iterrows():
        G.add_node(str(row['account_id']))

    edge_count = 0
    for _, row in edges_df.iterrows():
        src = str(row['from_account'])
        dst = str(row['to_account'])

        src_r = reason_lookup.get(src, 'normal')
        dst_r = reason_lookup.get(dst, 'normal')

        if src_r == 'fraud_cycle' or dst_r == 'fraud_cycle':
            edge_reason = 'fraud_cycle'

        elif src_r == 'amount_anomaly' or dst_r == 'amount_anomaly':
            edge_reason = 'amount_anomaly'

        elif src_r == 'high_velocity' or dst_r == 'high_velocity':
            edge_reason = 'high_velocity'

        elif (src_r == 'amount_anomaly' or src_r == 'high_velocity' or
            dst_r == 'amount_anomaly' or dst_r == 'high_velocity'):
            edge_reason = 'suspicious'
        
        else:   
            edge_reason = 'normal'


        G.add_edge(src, dst, reason=edge_reason)
        edge_count += 1

    # Trim to slider limit
    visible_nodes = set(sorted(G.nodes())[:max_graph_nodes])
    G_sub = G.subgraph(visible_nodes).copy()

    # ── Draw ──────────────────────────────────────────────────────────────
    fig, ax = plt.subplots(figsize=(12, 8))
    fig.patch.set_facecolor('#0d1117')
    ax.set_facecolor('#0d1117')

    pos = nx.spring_layout(G_sub, k=1.5, seed=42, iterations=50)

    # Node styling
    node_colors = [
        NODE_COLOR_FLAGGED if n in flagged_set else NODE_COLOR_NORMAL
        for n in G_sub.nodes()
    ]
    node_sizes = [
        600 if n in flagged_set else 200
        for n in G_sub.nodes()
    ]
  #this is default
    # Edge styling — pull colour from EDGE_COLOR_MAP
    # edge_colors = [
    #     EDGE_COLOR_MAP.get(G_sub[u][v].get('reason', 'normal'), EDGE_COLOR_MAP['normal'])
    #     for u, v in G_sub.edges()
    # ]
    edge_colors = []

    for u, v in G_sub.edges():
        reason = G_sub[u][v].get('reason', 'normal')

        # If fraud cycle highlighting disabled
        if reason == 'fraud_cycle' and not highlight_cycles:
            edge_colors.append(EDGE_COLOR_MAP['normal'])
        else:
            edge_colors.append(
                EDGE_COLOR_MAP.get(reason, EDGE_COLOR_MAP['normal'])
            )

    nx.draw_networkx_nodes(
        G_sub, pos,
        node_color=node_colors, node_size=node_sizes, alpha=0.9, ax=ax
    )
    nx.draw_networkx_edges(
        G_sub, pos,
        edge_color=edge_colors, alpha=0.85,
        arrows=True, arrowsize=15, width=2.0,
        connectionstyle='arc3,rad=0.1', ax=ax
    )

    if show_labels and len(G_sub.nodes()) <= 80:
        labels = {
            n: f"{n}\n({reason_lookup.get(n, 'normal')})"
            for n in G_sub.nodes()
        }
        nx.draw_networkx_labels(
            G_sub, pos, labels=labels,
            font_color='#e6edf3', font_size=7, ax=ax
        )

    # ── LEGEND ────────────────────────────────────────────────────────────
    # Nodes → filled Patch  |  Edges → Line2D (guarantees correct line colour)
    legend_handles = [
    mpatches.Patch(
        facecolor=NODE_COLOR_FLAGGED,
        edgecolor=NODE_COLOR_FLAGGED,
        label='Suspicious Accounts (Nodes)'
    ),
    mpatches.Patch(
        facecolor=NODE_COLOR_NORMAL,
        edgecolor=NODE_COLOR_NORMAL,
        label='Normal Accounts (Nodes)'
    ),

    mlines.Line2D([], [], color=EDGE_COLOR_MAP['fraud_cycle'], linewidth=2.5, label='Fraud Cycle Edge'),
    mlines.Line2D([], [], color=EDGE_COLOR_MAP['high_velocity'], linewidth=2.5, label='High Velocity Edge'),
    mlines.Line2D([], [], color=EDGE_COLOR_MAP['amount_anomaly'], linewidth=2.5, label='Amount Anomaly Edge'),
    mlines.Line2D([], [], color=EDGE_COLOR_MAP['suspicious'], linewidth=2.5, label='Suspicious Edge'),
    mlines.Line2D([], [], color=EDGE_COLOR_MAP['normal'], linewidth=2.5, label='Normal Edge'),
]

    ax.legend(
        handles=legend_handles,
        loc='upper left',
        facecolor='#161b22',
        edgecolor='#444d56',
        labelcolor='#e6edf3',
        fontsize=9,
        framealpha=0.92,
        borderpad=0.8,
        labelspacing=0.5,
    )

    ax.set_title(
        f"Transaction Network — {len(G_sub.nodes())} accounts, {edge_count} edges",
        color='#e6edf3', fontsize=13, pad=15
    )
    ax.axis('off')
    plt.tight_layout()
    st.pyplot(fig)
    plt.close()

# ─────────────────────────────────────────────────────────────────────────────
# Fraud Alerts panel
# ─────────────────────────────────────────────────────────────────────────────
with col_alerts:
    st.markdown("### 🚨 Fraud Alerts")

    if flagged == 0:
        st.success("✅ No suspicious accounts detected.")
    else:
        show_all = st.checkbox("Show all alerts", value=False)

        flagged_df     = accounts_df[accounts_df['is_flagged'] == 1]
        display_alerts = flagged_df if show_all else flagged_df.head(10)
        total_flagged  = len(flagged_df)

        st.caption(f"Showing {len(display_alerts)} of {total_flagged} suspicious accounts")

        for reason, group in display_alerts.groupby('flag_reason'):
            reason_label = {
                'fraud_cycle':             '🔄 Fraud Cycle',
                'high_velocity':           '⚡ High Velocity',
                'amount_anomaly':          '💰 Amount Anomaly',
                'suspicious_cluster':      '🕸️ Suspicious Cluster',
                'high_velocity_or_amount': '⚡ High Velocity / Amount',
            }.get(reason, f'⚠️ {reason}')

            st.markdown(f"**{reason_label}** — {len(group)} accounts")

            for _, row in group.iterrows():
                st.markdown(
                    f'<div class="fraud-alert">⚠️ {row["account_id"]} '
                    f'| Sent: ${row["total_sent"]:,.0f} '
                    f'| Rcvd: ${row["total_received"]:,.0f}</div>',
                    unsafe_allow_html=True
                )
            st.markdown("")

        if not show_all and total_flagged > 10:
            st.info(f"Showing top 10 of {total_flagged}. Enable 'Show all alerts' for the full list.")

    if fraud_cycles:
        st.markdown("---")
        st.markdown("**🔄 Detected Fraud Cycles**")
        for i, cycle in enumerate(fraud_cycles[:5]):
            st.markdown(f"**Cycle {i+1}:** `{'→'.join(cycle)}`")
        if len(fraud_cycles) > 5:
            st.caption(f"...and {len(fraud_cycles)-5} more cycles")

# ─────────────────────────────────────────────────────────────────────────────
# Account Table + Amount Distribution
# ─────────────────────────────────────────────────────────────────────────────
st.markdown("---")
col_table, col_hist = st.columns([3, 2])

with col_table:
    st.markdown("### 📋 Account Summary")
    display_df = accounts_df.copy()
    display_df['Status']         = display_df['is_flagged'].apply(
        lambda x: '🚨 SUSPICIOUS' if x else '✅ Normal'
    )
    display_df['total_sent']     = display_df['total_sent'].apply(lambda x: f"${x:,.2f}")
    display_df['total_received'] = display_df['total_received'].apply(lambda x: f"${x:,.2f}")
    display_df = display_df.rename(columns={
        'account_id':     'Account',
        'total_sent':     'Sent',
        'total_received': 'Received',
        'tx_count':       'Tx Count',
        'flag_reason':    'Reason',
    })
    st.dataframe(
        display_df[['Account', 'Sent', 'Received', 'Tx Count', 'Status', 'Reason']],
        use_container_width=True,
        height=300
    )
  
with col_hist:
    st.markdown("### 📈 Transaction Amount Distribution")

    fig2, ax2 = plt.subplots(figsize=(7, 4))
    fig2.patch.set_facecolor('#0d1117')
    ax2.set_facecolor('#161b22')

    # Remove extreme outliers
    amounts = df['amount']
    amounts = amounts[amounts > 0]
    amounts = amounts.clip(upper=amounts.quantile(0.95))

    # Better bins for log scale
    bins = np.logspace(
        np.log10(amounts.min()),
        np.log10(amounts.max()),
        20
    )

    ax2.hist(
        amounts,
        bins=bins,
        color='#388bfd',
        alpha=0.85,
        edgecolor='#21262d'
    )

    # Logarithmic X-axis
    ax2.set_xscale('log')

    ax2.set_xlabel(
        'Transaction Amount ($) [Log Scale]',
        color='#8b949e',
        fontsize=10
    )

    ax2.set_ylabel(
        'Frequency',
        color='#8b949e',
        fontsize=10
    )

    ax2.set_title(
        'Transaction Distribution',
        color='#e6edf3',
        fontsize=12
    )

    ax2.tick_params(colors='#8b949e')

    for spine in ax2.spines.values():
        spine.set_edgecolor('#30363d')

    ax2.grid(alpha=0.15)

    plt.tight_layout()
    st.pyplot(fig2)
    plt.close()

# ─────────────────────────────────────────────────────────────────────────────
# Raw Data
# ─────────────────────────────────────────────────────────────────────────────
with st.expander("🗂️ View Raw Transaction Data"):
    st.dataframe(df, use_container_width=True, height=250)

# ─────────────────────────────────────────────────────────────────────────────
# Footer
# ─────────────────────────────────────────────────────────────────────────────
st.markdown("---")
st.markdown(
    "<center style='color:#8b949e; font-size:0.85em;'>"
    "🏦 Fraud Detection System &nbsp;|&nbsp; "
    "BFS/DFS Graph Algorithms &nbsp;|&nbsp; "
    "C++17 Backend + Python Streamlit Frontend &nbsp;|&nbsp; "
    "Inspired by JPMorgan AML Systems"
    "</center>",
    unsafe_allow_html=True
)