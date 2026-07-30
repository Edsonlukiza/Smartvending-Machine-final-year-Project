from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.responses import HTMLResponse
import uvicorn
import json
import asyncio
from uuid import uuid4
from datetime import datetime
import qrcode

app = FastAPI()

class ConnectionManager:
    def __init__(self):
        self.active_connections: dict[str, WebSocket] = {}

    async def connect(self, machine_id: str, websocket: WebSocket):
        await websocket.accept()
        self.active_connections[machine_id] = websocket

    def disconnect(self, machine_id: str):
        if machine_id in self.active_connections:
            del self.active_connections[machine_id]

    async def send_command(self, machine_id: str, message: dict):
        if machine_id in self.active_connections:
            await self.active_connections[machine_id].send_text(json.dumps(message))
            return True
        return False

manager = ConnectionManager()

PRICES = {
    1: 1500,
    2: 1400,
    3: 1000
}

ORDERS: dict[str, dict] = {}

@app.get("/")
async def root_redirect():
    return {"status": "ok", "message": "Backend running. Use /checkout after order creation."}

@app.get("/checkout", response_class=HTMLResponse)
async def checkout_page(request: Request):
    return """
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset='UTF-8'>
      <meta name='viewport' content='width=device-width, initial-scale=1.0'>
      <title>Vending Payment</title>
      <style>
        body { font-family: Arial, sans-serif; background: #111; color: #fff; margin: 0; }
        .card { max-width: 440px; margin: 40px auto; background: #1e293b; border-radius: 16px; padding: 24px; box-shadow: 0 20px 50px rgba(0,0,0,0.35); }
        h1, h2 { margin: 0 0 14px; }
        label { display: block; margin-top: 16px; color: #94a3b8; }
        input { width: 100%; padding: 12px; margin-top: 8px; border-radius: 10px; border: 1px solid #334155; background: #0f172a; color: #fff; }
        button { width: 100%; padding: 14px; border: none; border-radius: 10px; background: #2563eb; color: #fff; font-size: 16px; cursor: pointer; }
        button:hover { background: #1d4ed8; }
        .status { margin-top: 16px; color: #fbbf24; font-weight: 600; }
      </style>
    </head>
    <body>
      <div class='card'>
        <h1>Complete Payment</h1>
        <p id='summary'>Loading order details…</p>
        <label for='phone'>Phone number</label>
        <input id='phone' type='text' placeholder='0712345678'>
        <button id='payBtn'>Pay Now</button>
        <div id='status' class='status'></div>
      </div>

      <script>
        const query = new URLSearchParams(window.location.search);
        const orderId = query.get('order_id');
        const statusEl = document.getElementById('status');
        const summaryEl = document.getElementById('summary');

        if (!orderId) {
          summaryEl.textContent = 'No order ID found in this URL.';
        } else {
          summaryEl.textContent = 'Order ID: ' + orderId + '. Enter your phone number and complete payment.';
        }

        document.getElementById('payBtn').addEventListener('click', async () => {
          const phone = document.getElementById('phone').value.trim();
          if (!orderId || !phone) {
            statusEl.textContent = 'Please enter a phone number and ensure the URL contains an order ID.';
            statusEl.style.color = '#f87171';
            return;
          }

          statusEl.style.color = '#fbbf24';
          statusEl.textContent = 'Processing payment...';

          const response = await fetch('/pay', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({order_id: orderId, phone})
          });
          const data = await response.json();

          if (data.status === 'success') {
            statusEl.style.color = '#34d399';
            statusEl.textContent = 'Payment successful. Dispensing your item now.';
          } else {
            statusEl.style.color = '#f87171';
            statusEl.textContent = data.message || 'Payment failed.';
          }
        });
      </script>
    </body>
    </html>
    """

@app.post("/order")
async def create_order(request: Request):
    body = await request.json()
    machine_id = body.get('machine_id')
    drink_id = body.get('drink_id')

    if not machine_id or drink_id not in PRICES:
        return {"status": "error", "message": "Invalid machine or drink selection."}

    order_id = uuid4().hex[:10]
    price = PRICES[drink_id]
    checkout_url = str(request.url_for('checkout')) + f'?order_id={order_id}'

    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M, box_size=4, border=2)
    qr.add_data(checkout_url)
    qr.make(fit=True)
    matrix = qr.get_matrix()
    qr_list = [1 if cell else 0 for row in matrix for cell in row]

    ORDERS[order_id] = {
        'order_id': order_id,
        'machine_id': machine_id,
        'drink_id': drink_id,
        'price': price,
        'status': 'pending',
        'created_at': datetime.utcnow().isoformat() + 'Z',
        'checkout_url': checkout_url,
        'qr_size': len(matrix),
        'qr_data': qr_list
    }

    return {
        'status': 'success',
        'order_id': order_id,
        'price': price,
        'payment_url': checkout_url,
        'qr_size': len(matrix),
        'qr_data': qr_list
    }

@app.get('/order/{order_id}')
async def get_order(order_id: str):
    order = ORDERS.get(order_id)
    if not order:
        return {"status": "error", "message": "Order not found."}
    return {"status": "success", "order": order}

@app.post('/pay')
async def pay_endpoint(req: Request):
    data = await req.json()
    order_id = data.get('order_id')
    phone = data.get('phone')

    if not order_id or not phone:
        return {"status": "error", "message": "Order ID and phone are required."}

    order = ORDERS.get(order_id)
    if not order:
        return {"status": "error", "message": "Order not found."}
    if order['status'] != 'pending':
        return {"status": "error", "message": "Order is not pending payment."}

    await asyncio.sleep(1.0)
    order['status'] = 'paid'
    order['paid_at'] = datetime.utcnow().isoformat() + 'Z'
    order['phone'] = phone

    payload = {
        'action': 'dispense',
        'drink_id': order['drink_id'],
        'order_id': order_id
    }
    success = await manager.send_command(order['machine_id'], payload)
    if success:
        return {
            'status': 'success',
            'message': 'Payment successful and dispense signal sent.',
            'order_id': order_id,
            'price': order['price']
        }

    return {"status": "error", "message": "Vending machine connection not found."}

@app.get('/pay')
async def pay_get():
    return {"status": "ok", "message": "Use POST /pay after order creation."}

@app.websocket('/ws/{machine_id}')
async def websocket_endpoint(websocket: WebSocket, machine_id: str):
    await manager.connect(machine_id, websocket)
    print(f"Machine {machine_id} connected")
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(machine_id)
        print(f"Machine {machine_id} disconnected")

if __name__ == '__main__':
    uvicorn.run(app, host='0.0.0.0', port=8000)
