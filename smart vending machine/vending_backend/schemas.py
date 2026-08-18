from pydantic import BaseModel


class ProductResponse(BaseModel):
    id: int
    name: str
    price: int
    quantity: int

    class Config:
        from_attributes = True


class OrderItemRequest(BaseModel):
    product_id: int
    quantity: int


class OrderCreateRequest(BaseModel):
    machine_id: str
    items: list[OrderItemRequest]