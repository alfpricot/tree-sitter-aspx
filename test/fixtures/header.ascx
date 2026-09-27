<%@ Control Language="vb" AutoEventWireup="false" Inherits="Example.Switcher" %>
<div id="switcher">
    <%: CurrentView %> view | <a href="<%: SwitchUrl %>">Switch to <%: AlternateView %></a>
</div>
