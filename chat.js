
var sock;

$(document).ready( function() {
   sock = new WebSocket( "ws://zarchat.interfinitydynamics.info/chat_sock" );

   sock.onopen = function( e ) {
      console.log( 'socket open!' );
      sock.send( "xxx yyy zzz" );
   };
} );

