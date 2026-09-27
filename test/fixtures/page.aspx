<%@ Page Language="C#" AutoEventWireup="true" CodeBehind="Home.aspx.cs" Inherits="Example.Home" %>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head runat="server">
    <meta charset="utf-8" />
    <title>Example</title>
    <style type="text/css">
        .caption { font-size: 15pt; font-family: "Arial"; }
    </style>
</head>
<script type="text/javascript">
    function preventBack() { if (1 < 2) { window.history.forward(); } }
</script>
<body>
    <form id="Main" runat="server">
        <table>
            <tr>
                <td>
                    <asp:Label ID="Caption" runat="server" Text="Example" Font-Bold="True"></asp:Label>
                    <br />
                    <asp:TextBox ID="Badge" runat="server" AutoPostBack="true" autocomplete="off" />
                    <asp:Button ID="Clear" runat="server" Text="Clear" OnClick="Clear_Click" />
                    <asp:GridView ID="Rows" runat="server">
                        <Columns>
                            <asp:BoundField DataField="Name" />
                        </Columns>
                    </asp:GridView>
                </td>
            </tr>
        </table>
    </form>
</body>
</html>
